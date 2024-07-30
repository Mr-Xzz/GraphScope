
/** Copyright 2020 Alibaba Group Holding Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * 	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "flex/engines/graph_db/app/app_base.h"
#include "flex/storages/rt_mutable_graph/types.h"
#include "flex/utils/property/types.h"

#include "flex/engines/hqps_db/database/mutable_csr_interface_v2.h"
#include "flex/proto_generated_gie/results.pb.h"

// A unit test case which shows how to customize a graph interface, and write a
// stored procedure and run.

struct MyColumnBase {
  virtual gs::Any get(size_t index) = 0;
  virtual ~MyColumnBase() = default;
};

template <typename T>
struct MyColumn : public MyColumnBase {
  T get_view(size_t index) const { return T(); }
  [[nodiscard]] gs::Any get(size_t index) const { return gs::Any(T()); }
};

template <typename T>
struct PropertyGetter {
  using value_type = T;
  std::shared_ptr<MyColumn<T>> column;
  bool is_valid;
  explicit PropertyGetter(std::shared_ptr<MyColumn<T>> column)
      : column(column), is_valid((column != nullptr)) {}

  [[nodiscard]] bool IsValid() const { return is_valid; }

  inline T Get(size_t index) const { return column->get_view(index); }

  inline T get_view(size_t index) const { return column->get_view(index); }
};

class NbrList {
 public:
  using vid_t = uint64_t;
  using Iterator = typename std::vector<vid_t>::const_iterator;
//  class Iterator {};
  [[nodiscard]] Iterator begin() const {
    return nbrs_.begin();
  }
  [[nodiscard]] Iterator end() const {
    return nbrs_.end();
  }
  inline size_t size() const {
    return nbrs_.size();
  }

 private:
  std::vector<vid_t> nbrs_;
};

class NbrListArray {
 public:
  NbrListArray() = default;
  ~NbrListArray() = default;
  // 返回指定索引处的 NbrList
  [[nodiscard]] NbrList get(size_t index) const {
    if (index < lists_.size()) {
      return lists_[index];
    }
    throw std::out_of_range("Index out of range");
  }
  // 返回 NbrList 数组的大小
  [[nodiscard]] size_t size() const {
    return lists_.size();
  }
  // 改变 NbrList 数组的大小
  void resize(size_t new_size) {
    lists_.resize(new_size);
  }
 private:
  std::vector<NbrList> lists_; // 存储多个 NbrList 的向量
};


template <typename T>
class AdjList {
  class Iterator {};

 public:
  Iterator begin() const;
  Iterator end() const;
  size_t size() const;
};

//提供自己的实现
class SubGraph {
 public:
  using vid_t = uint64_t;
  using label_id_t = uint8_t;
  class Iterator {
    inline void Next() const;

    inline vid_t GetDstId() const;

    inline vid_t GetSrcId() const;

    inline vid_t GetOtherId() const;

    inline label_id_t GetDstLabel() const;

    inline label_id_t GetSrcLabel() const;

    inline label_id_t GetOtherLabel() const;

    inline gs::Direction GetDirection();

    inline gs::Any GetData() const;
    inline bool IsValid() const;
  };
  inline Iterator get_edges(vid_t vid) const;

  // here the src, dst, refer the sub graph, not the csr.
  label_id_t GetSrcLabel() const;
  label_id_t GetDstLabel() const;
  label_id_t GetEdgeLabel() const;
  gs::Direction GetDirection() const;
};

/**
 * @brief Stores a list of AdjLists, each of which represents the edges of a
 * vertex.
 * @tparam T The type of the property.
 */
template <typename T>
class AdjListArray {
 public:
  size_t size() const;

  AdjList<T> get(size_t i) const;
};

// Real implementation of the storage
class ActualStorage {
 public:
  using vertex_id_t = uint64_t;
  using label_id_t = uint16_t;

  const std::string kLabelConnector = "::";

  // 定义5个string数组，使用vector容器，存储原始数据
  std::vector<std::string> relationTypes;
  std::vector<std::string> srcDomainAndTypes;
  std::vector<std::string> destDomainAndTypes;
  std::vector<std::string> srcIDs;
  std::vector<std::string> destIDs;

  // 定义图接口所需要的数据结构
  std::unordered_map<std::string, label_id_t> vertexLabelToId;
  std::unordered_map<label_id_t, std::string> vertexIdToLabel;
  unsigned int vertexLabelNum;
  std::unordered_map<std::string, label_id_t> edgeLabelToId;
  std::unordered_map<label_id_t, std::string> edgeIdToLabel;
  unsigned int edgeLabelNum;
  std::unordered_map<std::string, std::vector<std::pair<std::string, gs::PropertyType>>> edgeTripleToProperty;
  std::unordered_map<label_id_t, std::vector<std::pair<std::string, gs::PropertyType>>> vertexToProperty;

  std::unordered_map<std::string, vertex_id_t> vertexToId;
  std::unordered_map<vertex_id_t, std::string> IdToVertex;
  std::unordered_map<label_id_t, std::vector<vertex_id_t>> vertexLabelIdToIds;

  std::unordered_set<std::string> edges;
  std::unordered_map<std::string, uint16_t> edgeTripleToNum;

  inline void Init();
  void LoadData() {
    // 定义文件名
    std::istringstream ssTotal;
    std::string filename = "/workspace/GraphScope-zl/flex/tests/interface/example_data.csv";

    // 如果文件存在，打开文件
    std::ifstream file(filename);
    if (!file) {
      std::cerr << "无法打开文件: " << filename << std::endl;
      throw std::runtime_error("无法打开文件");
    }

    // 使用 std::ostringstream 读取文件内容
    std::ostringstream oss;
    oss << file.rdbuf();
    // 关闭文件
    file.close();

    // 将读入的字符串存储到 std::istringstream 中
    ssTotal.str(oss.str());
    std::string line;
    std::getline(ssTotal, line);
    while (std::getline(ssTotal, line)) {
      std::istringstream ss(line);
      std::string token;
      // 获取，分隔的值
      std::getline(ss, token, ',');
      relationTypes.push_back(token);
      std::getline(ss, token, ',');
      srcDomainAndTypes.push_back(token);
      std::getline(ss, token, ',');
      destDomainAndTypes.push_back(token);
      std::getline(ss, token, ',');
      srcIDs.push_back(token);
      std::getline(ss, token, ',');
      destIDs.push_back(token);
    }
    // 输出结果，以确保数据读取正确
    int totalOutputCount = 0;
    std::cout<< "Total size: " << relationTypes.size() << ". Sample examples ↓" << std::endl;
    for (size_t i = 0; i < relationTypes.size(); ++i) {
      if (totalOutputCount++ < 10) {
        std::cout << relationTypes[i] << ", "
                  << srcDomainAndTypes[i] << ", "
                  << destDomainAndTypes[i] << ", "
                  << srcIDs[i] << ", "
                  << destIDs[i] << std::endl;
      }
    }
    Init();
    std::cout << "Initialize graph interface successfully." << std::endl;
  }
};

//基于实际的存储实现，封装出访问接口
class TestGraph {
 public:
  using vertex_id_t = uint64_t;
  using label_id_t = uint16_t;

  explicit TestGraph(const ActualStorage& storage) : storage_(storage) {}

  template <typename T>
  using adj_list_array_t = AdjListArray<T>;

  using nbr_list_array_t = NbrListArray;

  using sub_graph_t = SubGraph;

  template <typename T>
  using prop_getter_t = gs::mutable_csr_graph_impl::PropertyGetter<T>;

  using untyped_prop_getter_t =
      gs::mutable_csr_graph_impl::UntypedPropertyGetter;

  //////////////////////////////Graph Metadata Related////////////
  [[nodiscard]] inline size_t VertexLabelNum() const {
    return storage_.vertexLabelNum;
  }

  [[nodiscard]] inline size_t EdgeLabelNum() const {
    return storage_.edgeLabelNum;
  }

  [[nodiscard]] inline size_t VertexNum() const {
    return storage_.vertexToId.size();
  }

  [[nodiscard]] inline size_t VertexNum(const label_id_t& label) const {
    auto it = storage_.vertexLabelIdToIds.find(label);
    if (it != storage_.vertexLabelIdToIds.end()) {
      return it -> second.size();
    } else {
      return 0;
    }
  }

  [[nodiscard]] inline size_t EdgeNum() const {
    return storage_.edges.size();
  }

  [[nodiscard]] inline size_t EdgeNum(label_id_t src_label, label_id_t dst_label,
                        label_id_t edge_label) const {

    std::string edgeTriple = std::to_string(edge_label) + storage_.kLabelConnector +
                             std::to_string(src_label) + storage_.kLabelConnector + std::to_string(dst_label);
    auto it = storage_.edgeTripleToNum.find(edgeTriple);
    if (it != storage_.edgeTripleToNum.end()) {
      return it -> second;
    } else {
      return 0;
    }
  }

  [[nodiscard]] label_id_t GetVertexLabelId(const std::string& label) const {
    return storage_.vertexLabelToId.at(label);
  }

  [[nodiscard]] label_id_t GetEdgeLabelId(const std::string& label) const {
    return storage_.edgeLabelToId.at(label);
  }

  [[nodiscard]] std::string GetVertexLabelName(label_id_t index) const {
    return storage_.vertexIdToLabel.at(index);
  }

  [[nodiscard]] std::string GetEdgeLabelName(label_id_t index) const {
    return storage_.edgeIdToLabel.at(index);
  }

  [[nodiscard]] bool ExitVertexLabel(const std::string& label) const {
    return storage_.vertexLabelToId.count(label) > 0;
  }

  [[nodiscard]] bool ExitEdgeLabel(const std::string& edge_label) const {
    return storage_.edgeLabelToId.count(edge_label) > 0;
  }

  [[nodiscard]] bool ExitEdgeTriplet(const label_id_t& src_label, const label_id_t& dst_label,
                       const label_id_t& edge_label) const {
    std::string key = std::to_string(edge_label) + storage_.kLabelConnector +
                          std::to_string(src_label) + storage_.kLabelConnector +
                          std::to_string(dst_label);
    return storage_.edgeTripleToProperty.count(key) > 0;
  }

  [[nodiscard]] std::vector<std::pair<std::string, gs::PropertyType>>
  GetEdgeTripletPropertyMeta(const label_id_t& src_label,
                             const label_id_t& dst_label,
                             const label_id_t& label) const {
    std::string key = std::to_string(label) + storage_.kLabelConnector +
                      std::to_string(src_label) + storage_.kLabelConnector +
                      std::to_string(dst_label);
    return storage_.edgeTripleToProperty.at(key);
  }

  [[nodiscard]] std::vector<std::pair<std::string, gs::PropertyType>> GetVertexPropertyMeta(
      label_id_t label) const {
    return storage_.vertexToProperty.at(label);
  }

  //////////////////////////////Vertex-related Interface////////////

  /**
   * @brief
    Scan all points with label label_id, for each point, get the properties
   specified by selectors, and input them into func. The function signature of
   func_t should be: void func(vertex_id_t v, const std::tuple<xxx>& props)
    Users implement their own logic in the function. This function has no return
   value. In the example below, we scan all person points, find all points with
   age , and save them to a vector. std::vector<vertex_id_t> vids;
       graph.ScanVertices(person_label_id,
   gs::PropertySelector<int32_t>("age"),
       [&vids](vertex_id_t vid, const std::tuple<int32_t>& props){
          if (std::get<0>(props) == 18){
              vids.emplace_back(vid);
          }
       });
    It is important to note that the properties specified by selectors will be
   input into the lambda function in a tuple manner.
   * @tparam FUNC_T
   * @tparam SELECTOR
   * @param label_id
   * @param selectors The Property selectors. The selected properties will be
   * fed to the function
   * @param func The lambda function for filtering.
   */
  // TODO(zhanglei): fix filter_null in scan.h
  template <typename FUNC_T, typename... T>
  void ScanVertices(const label_id_t& label_id,
                    const std::tuple<gs::PropertySelector<T>...>& selectors,
                    const FUNC_T& func) const {
    for (auto v : storage_.vertexLabelIdToIds.at(label_id)) {
      // TODO 获取属性
//      auto props = GetPropertiesForVertex(v, selectors);
      std::tuple<T...> props{};
      // 调用用户提供的函数
      func(v, props);
    }
  }

  /**
   * @brief ScanVertices scans all vertices with the given label with give
   * original id.
   * @param label_id The label id.
   * @param oid The original id.  这里的 original id 是什么意思？
   * @param vid The result internal id.
   */
  [[nodiscard]] bool ScanVerticesWithOid(const label_id_t& label_id, gs::Any oid,
                           vertex_id_t& vid) const {
    try {
      vertex_id_t vid_temp = oid.AsInt64();
      if (storage_.IdToVertex.count(vid_temp)) {
        vid = vid_temp;
        return true;
      }
    } catch (std::exception& e) {
      std::cout<< "error in ScanVerticesWithOid: " << e.what() << std::endl;
    }
    return false;
  }

  /**
   * @brief GetVertexPropertyGetter gets the property getter for the given
   * vertex label and property name.
   * @tparam T The property type.
   * @param label_id The vertex label id.
   * @param prop_name The property name.
   * @return The property getter.
   */
  template <typename T>
  [[nodiscard]] gs::mutable_csr_graph_impl::PropertyGetter<T> GetVertexPropertyGetter(
      const label_id_t& label_id, const std::string& prop_name) const {
    throw std::runtime_error("Not implemented");
  }

  [[nodiscard]] gs::mutable_csr_graph_impl::UntypedPropertyGetter
  GetUntypedVertexPropertyGetter(const label_id_t& label_id,
                                 const std::string& prop_name) const {
    throw std::runtime_error("Not implemented");
  }

  //////////////////////////////Edge-related Interface////////////

  /**
   * @brief GetEdges gets the edges with the given label and edge label, and
   * with the starting vertex internal ids.
   * When the direction is "out", the edges are from the source label to the
   * destination label, and vice versa when the direction is "in". When the
   * direction is "both", the src and dst labels SHOULD be the same.
   */
  template <typename T>
  AdjListArray<T> GetEdges(const label_id_t& src_label_id,
                           const label_id_t& dst_label_id,
                           const label_id_t& edge_label_id,
                           const std::vector<vertex_id_t>& vids,
                           const gs::Direction& direction,
                           size_t limit = INT_MAX) const {
    throw std::runtime_error("Not implemented");
  }

  /**
   * @brief Get vertices on the other side of edges, via the given edge label
   * and the starting vertex internal ids.
   * When the direction is "out", the vertices are on the destination label side
   * of the edges, and vice versa when the direction is "in". When the direction
   * is "both", the src and dst labels SHOULD be the same.
   */
  NbrListArray GetOtherVertices(const label_id_t& src_label_id,
                                const label_id_t& dst_label_id,
                                const label_id_t& edge_label_id,
                                const std::vector<vertex_id_t>& vids,
                                const gs::Direction& direction,
                                size_t limit = INT_MAX) const {
    throw std::runtime_error("Not implemented");
  }

  //////////////////////////////Subgraph-related Interface////////////
  gs::mutable_csr_graph_impl::SubGraph GetSubGraph(
      const label_id_t src_label_id, const label_id_t dst_label_id,
      const label_id_t edge_label_id, const gs::Direction& direction) const {
    throw std::runtime_error("Not implemented");
  }

 private:
  const ActualStorage& storage_;
};

//查询通过procedure的方式去实现。这块虚基类的接口还未确定好，但是确定的是就是一个query函数。
class ReadExample {
 public:
  using vertex_id_t = TestGraph::vertex_id_t;
  using label_id_t = TestGraph::label_id_t;

  ReadExample() = default;
  // Query function for query class
  results::CollectiveResults Query(TestGraph& graph) const {
    // Query the graph
    // Get the vertex label id
    label_id_t k8s_deployment_label_id = graph.GetVertexLabelId("k8s@deployment");
    // Get the property getter for the vertex label
    auto prop_getter =
        graph.GetVertexPropertyGetter<int32_t>(k8s_deployment_label_id, "age");
    // Get the property getter for the vertex label
    auto prop_getter2 =
        graph.GetVertexPropertyGetter<std::string>(k8s_deployment_label_id, "name");

    results::CollectiveResults results;
    // find the k8s_deployment with id 1
    vertex_id_t vid;
    if (graph.ScanVerticesWithOid(k8s_deployment_label_id, 1, vid)) {
      // Get the age of the person
      int32_t age = prop_getter.Get(vid);
      // Get the name of the person
      std::string name = prop_getter2.Get(vid);
      // Print the age and name
      std::cout << "k8s_deployment with id 1 has age: " << age << " and name: " << name
                << std::endl;
      auto record = results.add_results()->mutable_record();
      {
        auto col = record->add_columns();
        col->mutable_name_or_id()->set_name("age");
        col->mutable_entry()->mutable_element()->mutable_object()->set_i32(age);
      }
      {
        auto col = record->add_columns();
        col->mutable_name_or_id()->set_name("name");
        col->mutable_entry()->mutable_element()->mutable_object()->set_str(
            name);
      }

    } else {
      std::cout << "k8s_deployment with id 1 not found" << std::endl;
    }
    return results;
  }
};


class DescribeGraph {
 public:
  using vertex_id_t = TestGraph::vertex_id_t;
  using label_id_t = TestGraph::label_id_t;

  DescribeGraph() = default;
  // Query function for query class
  results::CollectiveResults Query(TestGraph& graph) const {
    // Query the graph Metadata
    size_t vertexLabelNum = graph.VertexLabelNum();
    size_t EdgeLabelNum = graph.EdgeLabelNum();
    size_t VertexNum = graph.VertexNum();
    size_t VertexNumWithId0 = graph.VertexNum(0);
    size_t EdgeNum = graph.EdgeNum();
    size_t EdgeNumWithTriple001 = graph.EdgeNum(0,1,0);
    std::string vertexLabelNameWithId0 = graph.GetVertexLabelName(0);
    std::string edgeLabelNameWithId0 = graph.GetEdgeLabelName(0);
    label_id_t vertexLabelId = graph.GetVertexLabelId(vertexLabelNameWithId0);
    label_id_t edgeLabelId = graph.GetEdgeLabelId(edgeLabelNameWithId0);
    bool vertexLabelExistSpecific = graph.ExitVertexLabel("k8s@deployment");
    bool edgeLabelExistSpecific = graph.ExitEdgeLabel("contains");
    bool existEdgeTriplet001 = graph.ExitEdgeTriplet(0,1,0);
    std::cout << "vertexLabelNum: " << vertexLabelNum << std::endl << "EdgeLabelNum: " << EdgeLabelNum << std::endl
              << "VertexNum: " << VertexNum << std::endl << "VertexNumWithId0: " << VertexNumWithId0 << std::endl
              << "EdgeNum: " << EdgeNum << std::endl << "EdgeNumWithTriple001: " << EdgeNumWithTriple001 << std::endl
              << "vertexLabelNameWithId0: " << vertexLabelNameWithId0 << std::endl << "edgeLabelNameWithId0: " << edgeLabelNameWithId0
              << std::endl << "vertexLabelId: " << vertexLabelId << std::endl << "edgeLabelId: " << edgeLabelId << std::endl
              << "vertexLabelExistSpecific: " << vertexLabelExistSpecific << std::endl << "edgeLabelExistSpecific: " << edgeLabelExistSpecific
              << std::endl << "existEdgeTriplet001: " << existEdgeTriplet001 << std::endl;

    results::CollectiveResults results;
    auto record = results.add_results()->mutable_record();
    {
      auto col = record->add_columns();
      col->mutable_name_or_id()->set_name("vertexLabelNum");
      col->mutable_entry()->mutable_element()->mutable_object()->set_i32(vertexLabelNum);
    }
    {
      auto col = record->add_columns();
      col->mutable_name_or_id()->set_name("EdgeLabelNum");
      col->mutable_entry()->mutable_element()->mutable_object()->set_i32(EdgeLabelNum);
    }
    {
      auto col = record->add_columns();
      col->mutable_name_or_id()->set_name("VertexNum");
      col->mutable_entry()->mutable_element()->mutable_object()->set_i32(VertexNum);
    }
    {
      auto col = record->add_columns();
      col->mutable_name_or_id()->set_name("EdgeNum");
      col->mutable_entry()->mutable_element()->mutable_object()->set_i32(EdgeNum);
    }
    return results;
  }
};



int main(int argc, char** argv) {
  //

  ActualStorage storage;
  storage.LoadData();
  // 使用std::chrono::seconds构造一个持续时间对象
  std::this_thread::sleep_for(std::chrono::seconds(1));

  TestGraph graph(storage);
  DescribeGraph describeGraph;
  describeGraph.Query(graph);
//  ReadExample app;
//  auto results = app.Query(graph);
  return 0;
}


void ActualStorage::Init() {
  int nextVertexId = 0;
  for(int i=0;i<2;i++) {
    auto target = i ? srcIDs : destIDs;
    for (const auto& item : target) {
      auto it = vertexToId.find(item);
      if (it == vertexToId.end()) {
        vertex_id_t id = nextVertexId++;
        vertexToId[item] = id;
        IdToVertex[id] = item;
      }
    }
  }


  int nextVertexLabelId = 0;
  for (long unsigned int i=0; i<relationTypes.size(); i++) {
    for (int j=0;j<2;j++) {
      auto& item = j ? srcDomainAndTypes.at(i) : destDomainAndTypes.at(i);
      auto& idStr = j ? srcIDs.at(i) : destIDs.at(i);
      auto it = vertexLabelToId.find(item);
      if (it == vertexLabelToId.end()) {
        label_id_t id = nextVertexLabelId++;
        vertexLabelToId[item] = id;
        vertexIdToLabel[id] = item;
        vertexLabelIdToIds[id] = std::vector<vertex_id_t>();
      } else {
        label_id_t id = vertexLabelToId[item];
        vertexLabelIdToIds[id].emplace_back(vertexToId[idStr]);
      }
    }
  }

  int nextEdgeLabelId = 0;
  for (const auto& item : relationTypes) {
    auto it = edgeLabelToId.find(item);
    if (it == edgeLabelToId.end()) {
      label_id_t id = nextEdgeLabelId++;
      edgeLabelToId[item] = id;
      edgeIdToLabel[id] = item;
    }
  }
  std::cout<<"edgeLabelToId.size():"<<edgeLabelToId.size()<<std::endl;
  vertexLabelNum = vertexLabelToId.size();
  edgeLabelNum = edgeLabelToId.size();

  for( long unsigned int i=0; i<relationTypes.size(); i++) {
    label_id_t relationTypeId = edgeLabelToId.find(relationTypes[i])->second;
    label_id_t srcLabelId = vertexLabelToId.find(srcDomainAndTypes[i])->second;
    label_id_t destLabelId = vertexLabelToId.find(destDomainAndTypes[i])->second;
    std::string edgeTriple = std::to_string(relationTypeId) + kLabelConnector +
                             std::to_string(srcLabelId) + kLabelConnector + std::to_string(destLabelId);
    if (edgeTripleToNum.count(edgeTriple)) {
      edgeTripleToNum[edgeTriple]++;
    } else {
      edgeTripleToNum[edgeTriple] = 1;
    }

    edges.insert(edgeTriple);
    std::string srcId = srcIDs[i];
    std::string destId = destIDs[i];

    // 目前示例数据里面都没有属性，所以先初始化一个空的vector
    if (edgeTripleToProperty.find(edgeTriple) == edgeTripleToProperty.end()) {
      std::cout<<edgeTriple<<std::endl;
      edgeTripleToProperty[edgeTriple] = std::vector<std::pair<std::string, gs::PropertyType>>();
    }
    if (vertexToProperty.find(srcLabelId) == vertexToProperty.end()){
      vertexToProperty[srcLabelId] = std::vector<std::pair<std::string, gs::PropertyType>>();
    }
    if (vertexToProperty.find(destLabelId) == vertexToProperty.end()){
      vertexToProperty[destLabelId] = std::vector<std::pair<std::string, gs::PropertyType>>();
    }
  }
}