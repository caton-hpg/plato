/*
  Tencent is pleased to support the open source community by making
  Plato available.
  Copyright (C) 2019 THL A29 Limited, a Tencent company.
  All rights reserved.

  Licensed under the BSD 3-Clause License (the "License"); you may
  not use this file except in compliance with the License. You may
  obtain a copy of the License at

  https://opensource.org/licenses/BSD-3-Clause

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" basis,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
  implied. See the License for the specific language governing
  permissions and limitations under the License.

  See the AUTHORS file for names of contributors.
*/

#include <cstdint>
#include <cstdlib>
#include <utility>

#include "plato/util/perf.hpp"
#include "plato/util/atomic.hpp"
#include "plato/graph/graph.hpp"
#include "plato/util/nebula_writer.h"

DEFINE_string(input,       "",     "input file, in csv format, without edge data");
DEFINE_string(output,      "",      "output directory");
DEFINE_bool(is_directed,   false,  "is graph directed or not");
DEFINE_string(source,      "",      "start from which vertex");
DEFINE_int32(alpha,        -1,     "alpha value used in sequence balance partition");
DEFINE_bool(part_by_in,    false,  "partition by in-degree");
DEFINE_uint32(type,        0,      "0 -- always pull, 1 -- push-pull, else -- push");
DEFINE_uint32(iterations,  20,     "number of iterations");
DEFINE_bool(need_encode,   false,                    "");
DEFINE_string(encoder,     "single","single or distributed vid encoder");
DEFINE_string(vtype,       "uint32",                 "");

using edge_value_t = double;
using bcsr_spec_t          = plato::bcsr_t<edge_value_t, plato::sequence_balanced_by_source_t>;
using partition_bcsr_t     = bcsr_spec_t::partition_t;
using state_distance_t     = plato::dense_state_t<edge_value_t, partition_bcsr_t>;
using bitmap_spec_t        = plato::bitmap_t<>;
using weight_top_t = std::set<std::pair<edge_value_t,plato::vid_t>>;
const edge_value_t INF = std::numeric_limits<edge_value_t>::max();

struct broadcast_message_t{
  plato::vid_t vid;
  edge_value_t weight;
};

template <typename VID_T>
inline typename std::enable_if<std::is_integral<VID_T>::value, VID_T>::type get_source_vid(const std::string& source) {
  return (VID_T)std::strtoul(FLAGS_source.c_str(), nullptr, 0);
}

template <typename VID_T>
inline typename std::enable_if<!std::is_integral<VID_T>::value, VID_T>::type get_source_vid(const std::string& source) {
  return source;
}

inline plato::vid_t to_vid_t(const plato::vid_t& vid) {
	return vid;
}

inline plato::vid_t to_vid_t(const std::string& vid) {
	return std::stoi(vid);
}

std::string weight_top_2_str(const weight_top_t& list) {
	std::string str;
	for (auto it = list.begin(); it != list.end(); it++) {
		str.append(reinterpret_cast<const char*>(&(it->first)), sizeof(edge_value_t));
		str.append(reinterpret_cast<const char*>(&(it->second)), sizeof(plato::vid_t));
	}
	return str;
}

weight_top_t str_2_weight_top(std::string str) {
	weight_top_t list;

	auto* begin = &str[0];
	auto* p_c = begin;
	while (p_c != begin + str.length()) {
		edge_value_t* weight = reinterpret_cast<edge_value_t*>(p_c);
		p_c += sizeof(edge_value_t);
		plato::vid_t* vid = reinterpret_cast<plato::vid_t*>(p_c);
		p_c += sizeof(plato::vid_t);
		list.insert(std::make_pair(*weight, *vid));
	}
	return list;
}

bool string_not_empty(const char*, const std::string& value) {
  if (0 == value.length()) { return false; }
  return true;
}

void init(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  google::LogToStderr();
}


template <typename VID_T>
void run_dijkstra() {
  plato::stop_watch_t watch;
  auto& cluster_info = plato::cluster_info_t::get_instance();
  watch.mark("t0");

  plato::vencoder_t<edge_value_t, VID_T> encoder_ptr = nullptr;
  plato::vid_encoder_t<edge_value_t, VID_T> single_data_encoder;
  plato::distributed_vid_encoder_t<edge_value_t, VID_T> distributed_data_encoder;
  if (FLAGS_need_encode) {
    if (FLAGS_encoder == "single") {
      encoder_ptr = &single_data_encoder;
    } else {
      encoder_ptr = &distributed_data_encoder;
    }
  }

  // load graph data
  plato::graph_info_t graph_info(FLAGS_is_directed);
  auto pbcsr = plato::create_bcsr_seqs_from_path<edge_value_t, VID_T>(&graph_info, FLAGS_input,
      plato::edge_format_t::CSV, plato::double_decoder,
      FLAGS_alpha, FLAGS_part_by_in, encoder_ptr);

  plato::eid_t edges = graph_info.edges_;
  if (false == graph_info.is_directed_) { edges = edges * 2; }

  state_distance_t distance(graph_info.max_v_i_, pbcsr->partitioner());
  weight_top_t weight_top_list;
  std::shared_ptr<bitmap_spec_t> active_current(new bitmap_spec_t(graph_info.vertices_));
  distance.fill(INF);

  // parse root vertex id
  VID_T source = get_source_vid<VID_T>(FLAGS_source);
  plato::vid_t root = 0;
  if(FLAGS_need_encode) {
    encoder_ptr->set_vids({source});
    std::vector<plato::vid_t> encoded_vids;
    encoder_ptr->get_vids(encoded_vids);
    DCHECK(encoded_vids.size() == 1);
    root = encoded_vids[0];
  } else {
    root = to_vid_t(source);
  }
  LOG(INFO) << "root:"<< root;

  distance[root] = 0;
  weight_top_list.insert(std::make_pair(0, root));

  auto partition_view = pbcsr->partitioner()->self_v_view();
  for (unsigned epoch_i = 0; weight_top_list.size() > 0 && epoch_i <= FLAGS_iterations; ++epoch_i) {
    LOG(INFO) << "partition_id:" << cluster_info.partition_id_ << ", epoch_i:" << epoch_i << ", weight_top_list.size():" << weight_top_list.size();

    watch.mark("t2");
    active_current->clear();
    auto top = *weight_top_list.begin();
    weight_top_list.erase(weight_top_list.begin());
    active_current->set_bit(top.second);
    auto active_view = plato::create_active_v_view(partition_view, *active_current);

    // only push
    plato::bc_opts_t opts;
    opts.local_capacity_ = 4 * PAGESIZE;
    plato::broadcast_message<broadcast_message_t, plato::vid_t> (active_view,
      [&](const plato::mepa_bc_context_t<broadcast_message_t>& context, plato::vid_t v_i) {
        context.send(broadcast_message_t{v_i,top.first});
      },
      [&](int /* p_i */, const broadcast_message_t& msg) {
        auto neighbours = pbcsr->neighbours(msg.vid);
        for (auto it = neighbours.begin_; neighbours.end_ != it; ++it) {
          plato::vid_t dst = it->neighbour_;
          if (distance[dst] > msg.weight + it->edata_ ) {
            if (distance[dst] != INF) {
               weight_top_list.erase(weight_top_list.find(std::make_pair(distance[dst], dst))); 
            }
            distance[dst] = msg.weight + it->edata_;
            LOG(INFO) <<  "partition_id:" << cluster_info.partition_id_ << ", " << dst << ", new distance:"<< distance[dst];
            weight_top_list.insert(std::make_pair(distance[dst], dst));
          }
        }
        return 1;
      }, opts);

    // broadcast weight_top_list
    weight_top_t weight_top_global;
    std::string weight_top_str = weight_top_2_str(weight_top_list);
    plato::broadcast_message<std::string, plato::vid_t> (active_view,
      [&](const plato::mepa_bc_context_t<std::string>& context, plato::vid_t v_i) {
        context.send(weight_top_str);
      },
      [&](int /* p_i */, const std::string& msg) {
        auto list = str_2_weight_top(msg);
        for(auto it = list.begin(); it != list.end(); it++){
          weight_top_global.insert(std::make_pair(it->first, it->second));
        }
        return weight_top_global.size();
      }, opts);

      weight_top_list.swap(weight_top_global);

 
  } // end iteration
  LOG(INFO) << "total cost: " << watch.show("t0") / 1000.0 << "s";

  distance.template foreach<int> (
      [&](plato::vid_t v_i, double* pval) {
        LOG(INFO) << "partition_id:" << cluster_info.partition_id_ << ", vid=" << v_i << ", distance=" << *pval;
        return 0;
      }
    );

  
  // save distance
  watch.mark("t1");
  {
    if (!boost::starts_with(FLAGS_output, "nebula:")) {
      plato::thread_local_fs_output os(FLAGS_output, (boost::format("%04d_") % cluster_info.partition_id_).str(), true);
      distance.template foreach<int> (
        [&](plato::vid_t v_i, double* pval) {
          auto& fs_output = os.local();
          if (encoder_ptr != nullptr) {
            fs_output << encoder_ptr->decode(v_i) << "," << *pval << "\n";
          } else {
            fs_output << v_i << "," << *pval << "\n";
          }
          return 0;
        }
      );
    } else {
      if (encoder_ptr != nullptr) {
        struct Item {
          VID_T vid;
          double pval;
          std::string toString() const {
            return std::to_string(pval);
          }
        };
        plato::thread_local_nebula_writer<Item> writer(FLAGS_output);
        LOG(INFO) << "thread_local_nebula_writer is constructed....";
        distance.template foreach<int> (
          [&](plato::vid_t v_i, double* pval) {
            auto& buffer = writer.local();
            buffer.add(Item{encoder_ptr->decode(v_i), *pval});
            return 0;
          }
        );
      } else {
        struct Item {
          plato::vid_t vid;
          double pval;
          std::string toString() const {
            return std::to_string(pval);
          }
        };
        plato::thread_local_nebula_writer<Item> writer(FLAGS_output);
        distance.template foreach<int> (
          [&](plato::vid_t v_i, double* pval) {
            auto& buffer = writer.local();
            buffer.add(Item{v_i, *pval});
            return 0;
          }
        );
      }
    }
  }
  if (0 == cluster_info.partition_id_) {
    LOG(INFO) << "save result cost: " << watch.show("t1") / 1000.0 << "s";
  }

}


int main(int argc, char** argv) {
  auto& cluster_info = plato::cluster_info_t::get_instance();
  init(argc, argv);
  cluster_info.initialize(&argc, &argv);

  //run_dijkstra<int32_t>();

  if (FLAGS_vtype == "uint32") {
    run_dijkstra<uint32_t>();
  } else if (FLAGS_vtype == "int32")  {
    run_dijkstra<int32_t>();
  } else if (FLAGS_vtype == "uint64") {
    run_dijkstra<uint64_t>();
  } else if (FLAGS_vtype == "int64") {
    run_dijkstra<int64_t>();
  } else if (FLAGS_vtype == "string") {
    run_dijkstra<std::string>();
  } else {
    LOG(FATAL) << "unknown vtype: " << FLAGS_vtype;
  }
  
  return 0;
}

