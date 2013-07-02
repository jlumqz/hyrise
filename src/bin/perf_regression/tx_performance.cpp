// Copyright (c) 2012 Hasso-Plattner-Institut fuer Softwaresystemtechnik GmbH. All rights reserved.

#include <string>

#include "gtest/gtest.h"
#include "gtest/gtest-bench.h"

#include "access/InsertScan.h"
#include "access/tx/Commit.h"
#include "helper/checked_cast.h"
#include "helper/types.h"
#include "storage/TableBuilder.h"
#include "storage/Store.h"
#include "io/shortcuts.h"
#include "io/TransactionManager.h"

using namespace hyrise;

class TXBase : public ::testing::Benchmark {
 protected:
  storage::store_ptr_t linxxxs;
  storage::atable_ptr_t one_row;

 public:
  TXBase() {
    SetNumIterations(10);
    SetWarmUp(2);
  }

  void BenchmarkSetUp() {
    TableBuilder::param_list list2;
    list2.append().set_type("INTEGER").set_name("col_0");
    list2.append().set_type("INTEGER").set_name("col_1");

    one_row = TableBuilder::build(list2);
    one_row->resize(1);
    one_row->setValue<hyrise_int_t>(0,0, 99);
    one_row->setValue<hyrise_int_t>(1,0, 999);

    linxxxs = checked_pointer_cast<Store>(Loader::shortcuts::load("test/lin_xxxs.tbl"));
  }
};

BENCHMARK_F(TXBase, simple_commit) {
  for(size_t i=0; i < 10000; ++i) {
    auto& txmgr = tx::TransactionManager::getInstance();
    auto ctx = txmgr.buildContext();

    access::InsertScan is;
    is.setTXContext(ctx);
    is.addInput(linxxxs);
    is.setInputData(one_row);
    is.execute();

    access::Commit c;
    c.addInput(linxxxs);
    c.setTXContext(ctx);
    c.execute();
  }
}
