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
#include "yas/types/std/unordered_map.hpp"

DEFINE_string(input,       "",     "input file, in csv format, without edge data");
DEFINE_string(output,      "",      "output directory");
DEFINE_bool(is_directed,   false,  "is graph directed or not");
DEFINE_int32(alpha,        -1,     "alpha value used in sequence balance partition");
DEFINE_bool(part_by_in,    false,  "partition by in-degree");
DEFINE_uint32(type,        0,      "0 -- always pull, 1 -- push-pull, else -- push");
DEFINE_uint32(iterations,  20,     "number of iterations");
DEFINE_bool(need_encode,   false,                    "");
DEFINE_string(encoder,     "single","single or distributed vid encoder");
DEFINE_string(vtype,       "uint32",                 "");

using edge_value_t         = double;
using bcsr_spec_t          = plato::bcsr_t<edge_value_t, plato::sequence_balanced_by_source_t>;
using partition_bcsr_t     = bcsr_spec_t::partition_t;
using weight_map_t         = std::unordered_map<plato::vid_t, edge_value_t>; // plato::dense_state_t<edge_value_t, partition_bcsr_t>;
using state_distance_t     = std::unordered_map<plato::vid_t, weight_map_t>;//plato::dense_state_t<weight_map_t, partition_bcsr_t>;
using bitmap_spec_t        = plato::bitmap_t<>;
const edge_value_t INF = std::numeric_limits<edge_value_t>::max();
const size_t k_mutex_num = 1024;

struct msg_t{
  plato::vid_t vid;
  weight_map_t weights;

  template<typename Ar>
	void serialize(Ar &ar) {
		ar & vid & weights;
	}
};

void init(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  google::LogToStderr();
}

template <typename VID_T>
void run_floyd() {
  plato::stop_watch_t watch;
  auto& cluster_info = plato::cluster_info_t::get_instance();
  watch.mark("t0");

  plato::vencoder_t<edge_value_t, VID_T> encoder_ptr = nullptr;
  if (FLAGS_need_encode) {
    if (FLAGS_encoder == "single") {
      plato::vid_encoder_t<edge_value_t, VID_T> single_data_encoder;
      encoder_ptr = &single_data_encoder;
    } else {
      plato::distributed_vid_encoder_t<edge_value_t, VID_T> distributed_data_encoder;
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

  // `distance` is used to save distance between u and v.
  // `disatance[b][a]=5.9` means a->b distance is 5.9
  state_distance_t distance;// (graph_info.max_v_i_, pbcsr->partitioner());

  // active vertices
  std::shared_ptr<bitmap_spec_t> active_current(new bitmap_spec_t(graph_info.vertices_));
  std::shared_ptr<bitmap_spec_t> active_next(new bitmap_spec_t(graph_info.vertices_));
  active_current->fill();
  std::mutex mutexes[k_mutex_num];

  // begin iteration
  auto partition_view = pbcsr->partitioner()->self_v_view();
  for (unsigned epoch_i = 0; epoch_i <= FLAGS_iterations; ++epoch_i) {
    plato::vid_t activies = active_current->count();
    LOG(INFO) << "partition_id:" << cluster_info.partition_id_ << ", epoch_i:" << epoch_i << ", activies:" << activies;
    if(activies <= 0) { break; }

    watch.mark("t2");
    auto active_view = plato::create_active_v_view(partition_view, *active_current);

    // only push
    plato::bc_opts_t opts;
    opts.local_capacity_ = 4 * PAGESIZE;
    active_next->clear();
    plato::broadcast_message<msg_t, plato::vid_t> (active_view,
      [&](const plato::mepa_bc_context_t<msg_t>& context, plato::vid_t v_i) {
        auto& weights = distance[v_i];
        context.send(msg_t{v_i, weights});
      },
      [&](int /* p_i */, const msg_t& msg) {
        auto weights = msg.weights;
        auto src = msg.vid;
        auto neighbours = pbcsr->neighbours(src);

        for (auto it = neighbours.begin_; neighbours.end_ != it; ++it) {
          auto dst = it->neighbour_;
          if(dst == src) { continue;}
          if(weights.empty()) {
            std::lock_guard<std::mutex> lock(mutexes[dst%k_mutex_num]);
            distance[dst][src] = it->edata_;
            active_next->set_bit(dst);
          } else {
            std::lock_guard<std::mutex> lock(mutexes[dst%k_mutex_num]);
            for(auto w : weights){
              auto src = w.first;
              auto exist = distance[dst].find(src) != distance[dst].end();
              auto condition1 = !exist;
              auto condition2 = exist && distance[dst][src] > w.second + it->edata_;
              if(condition1 || condition2) {
                  distance[dst][src] = w.second + it->edata_;
                  active_next->set_bit(dst);
              }
              
            }
          }

          
        }
        return 1;
      }, opts);
      std::swap(active_next, active_current);

 
  } // end iteration
  LOG(INFO) << "total cost: " << watch.show("t0") / 1000.0 << "s";

  // save distance
  watch.mark("t1");
  {
    if (!boost::starts_with(FLAGS_output, "nebula:")) {
      plato::thread_local_fs_output os(FLAGS_output, (boost::format("%04d_") % cluster_info.partition_id_).str(), true);
      
      #pragma omp parallel num_threads(cluster_info.threads_)
      for(auto& d : distance){
        auto dst = d.first;
        for(auto& w : d.second){
            auto& fs_output = os.local();
            auto src = w.first;
            if(dst == src) { continue;}
            if (encoder_ptr != nullptr) {
              fs_output << encoder_ptr->decode(src) << "," << encoder_ptr->decode(dst) << "," << w.second << "\n";
            } else {
              fs_output << src << "," << dst << "," << w.second << "\n";
            }
          }
      }

    } else {
        CHECK(false) << "Does not support output to nebula database.";
    }
  }
  
  LOG(INFO) << "partition_id:" << cluster_info.partition_id_ << ", save result cost: " << watch.show("t1") / 1000.0 << "s";

}


int main(int argc, char** argv) {
  auto& cluster_info = plato::cluster_info_t::get_instance();
  init(argc, argv);
  cluster_info.initialize(&argc, &argv);

  // check parameters
  CHECK(!boost::starts_with(FLAGS_output, "nebula:")) << "Does not support output to nebula database.";

  if (FLAGS_vtype == "uint32") {
    run_floyd<uint32_t>();
  } else if (FLAGS_vtype == "int32")  {
    run_floyd<int32_t>();
  } else if (FLAGS_vtype == "uint64") {
    run_floyd<uint64_t>();
  } else if (FLAGS_vtype == "int64") {
    run_floyd<int64_t>();
  } else if (FLAGS_vtype == "string") {
    run_floyd<std::string>();
  } else {
    LOG(FATAL) << "unknown vtype: " << FLAGS_vtype;
  }
  
  return 0;
}

