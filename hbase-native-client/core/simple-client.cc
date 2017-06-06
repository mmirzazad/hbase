/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <folly/Logging.h>
#include <folly/Random.h>
#include <gflags/gflags.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include "connection/rpc-client.h"
#include "core/client.h"
#include "core/get.h"
#include "core/put.h"
#include "core/scan.h"
#include "core/table.h"
#include "serde/server-name.h"
#include "serde/table-name.h"
#include "utils/time-util.h"

using hbase::Client;
using hbase::Configuration;
using hbase::Get;
using hbase::Scan;
using hbase::Put;
using hbase::Table;
using hbase::pb::TableName;
using hbase::pb::ServerName;
using hbase::TimeUtil;

DEFINE_string(table, "test_table", "What table to do the reads or writes");
DEFINE_string(row, "row_", "row prefix");
DEFINE_string(zookeeper, "localhost:2181", "What zk quorum to talk to");
DEFINE_uint64(num_rows, 10000, "How many rows to write and read");
DEFINE_bool(puts, true, "Whether to perform puts");
DEFINE_bool(gets, true, "Whether to perform gets");
DEFINE_bool(multigets, true, "Whether to perform multi-gets");
DEFINE_bool(scans, true, "Whether to perform scans");
DEFINE_bool(display_results, false, "Whether to display the Results from Gets");
DEFINE_int32(threads, 6, "How many cpu threads");

std::unique_ptr<Put> MakePut(const std::string &row) {
  auto put = std::make_unique<Put>(row);
  put->AddColumn("f", "q", row);
  return std::move(put);
}

std::string Row(const std::string &prefix, uint64_t i) {
  auto suf = folly::to<std::string>(i);
  return prefix + suf;
}

int main(int argc, char *argv[]) {
  google::SetUsageMessage("Simple client to get a single row from HBase on the comamnd line");
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();
  FLAGS_logtostderr = 1;
  FLAGS_stderrthreshold = 1;

  // Configuration
  auto conf = std::make_shared<Configuration>();
  conf->Set("hbase.zookeeper.quorum", FLAGS_zookeeper);
  conf->SetInt("hbase.client.cpu.thread.pool.size", FLAGS_threads);

  auto row = FLAGS_row;

  auto tn = std::make_shared<TableName>(folly::to<TableName>(FLAGS_table));
  auto num_puts = FLAGS_num_rows;

  auto client = std::make_unique<Client>(*conf);
  auto table = client->Table(*tn);

  auto start_ns = TimeUtil::GetNowNanos();

  // Do the Put requests
  if (FLAGS_puts) {
    LOG(INFO) << "Sending put requests";
    for (uint64_t i = 0; i < num_puts; i++) {
      table->Put(*MakePut(Row(FLAGS_row, i)));
    }

    LOG(INFO) << "Successfully sent  " << num_puts << " Put requests in "
              << TimeUtil::ElapsedMillis(start_ns) << " ms.";
  }

  // Do the Get requests
  if (FLAGS_gets) {
    LOG(INFO) << "Sending get requests";
    start_ns = TimeUtil::GetNowNanos();
    for (uint64_t i = 0; i < num_puts; i++) {
      auto result = table->Get(Get{Row(FLAGS_row, i)});
      if (FLAGS_display_results) {
        LOG(INFO) << result->DebugString();
      }
    }

    LOG(INFO) << "Successfully sent  " << num_puts << " Get requests in "
              << TimeUtil::ElapsedMillis(start_ns) << " ms.";
  }

  // Do the Multi-Gets
  if (FLAGS_multigets) {
    std::vector<hbase::Get> gets;
    for (uint64_t i = 0; i < num_puts; ++i) {
      hbase::Get get(Row(FLAGS_row, i));
      gets.push_back(get);
    }

    LOG(INFO) << "Sending multi-get requests";
    start_ns = TimeUtil::GetNowNanos();
    auto results = table->Get(gets);

    if (FLAGS_display_results) {
      for (const auto &result : results) LOG(INFO) << result->DebugString();
    }

    LOG(INFO) << "Successfully sent  " << gets.size() << " Multi-Get requests in "
              << TimeUtil::ElapsedMillis(start_ns) << " ms.";
  }

  // Do the Scan
  if (FLAGS_scans) {
    LOG(INFO) << "Starting scanner";
    start_ns = TimeUtil::GetNowNanos();
    Scan scan{};
    auto scanner = table->Scan(scan);

    uint64_t i = 0;
    auto r = scanner->Next();
    while (r != nullptr) {
      if (FLAGS_display_results) {
        LOG(INFO) << r->DebugString();
      }
      r = scanner->Next();
      i++;
    }

    LOG(INFO) << "Successfully iterated over  " << i << " Scan results in "
              << TimeUtil::ElapsedMillis(start_ns) << " ms.";
    scanner->Close();
  }

  table->Close();
  client->Close();

  return 0;
}
