// Copyright (c) 2012 Hasso-Plattner-Institut fuer Softwaresystemtechnik GmbH. All rights reserved.
#include "io/Loader.h"

#include <log4cxx/logger.h>

#include "io/EmptyLoader.h"
#include "io/LoaderException.h"
#include "io/ValidityTableGeneration.h"
#include "storage/AbstractTable.h"
#include "storage/LogarithmicMergeStrategy.h"
#include "storage/SequentialHeapMerger.h"
#include "storage/SimpleStore.h"
#include "storage/Store.h"
#include "storage/TableFactory.h"
#include "storage/TableGenerator.h"
#include "storage/MutableVerticalTable.h"

log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("hyrise.io.Loader"));

param_ref_member_impl(Loader::params, AbstractInput, Input)
param_ref_member_impl(Loader::params, AbstractHeader, Header)
param_member_impl(Loader::params, std::string, BasePath)
param_member_impl(Loader::params, AbstractTableFactory *, Factory)
param_member_impl(Loader::params, InsertOnlyParam, InsertOnly)
param_member_impl(Loader::params, bool, ModifiableMutableVerticalTable)
param_member_impl(Loader::params, bool, ReturnsMutableVerticalTable)
param_member_impl(Loader::params, bool, Compressed)
param_member_impl(Loader::params, hyrise::storage::c_atable_ptr_t, ReferenceTable)

Loader::params::params() :
  Input(nullptr),
  Header(nullptr),
  Factory(nullptr),
  BasePath(""),
  InsertOnly({false, 0}),
  ModifiableMutableVerticalTable(false),
  ReturnsMutableVerticalTable(false),
  Compressed(false),
  ReferenceTable()
{}

Loader::params::params(const Loader::params &other) :
  Factory(other.getFactory()),
  BasePath(other.getBasePath()),
  InsertOnly(other.getInsertOnly()),
  ModifiableMutableVerticalTable(other.getModifiableMutableVerticalTable()),
  ReturnsMutableVerticalTable(other.getReturnsMutableVerticalTable()),
  Compressed(other.getCompressed()) {
  if (other.Input != nullptr) Input = other.Input->clone();
  if (other.Header != nullptr) Header = other.Header->clone();
  if (other.ReferenceTable != nullptr) ReferenceTable = other.ReferenceTable;
}

Loader::params &Loader::params::operator= (const Loader::params &other) {
  if (this != &other) { // protect against invalid self-assignment
    if (Input != nullptr) delete Input;
    if (Header != nullptr) delete Header;
    Input = other.getInput()->clone();
    Header = other.getHeader()->clone();
    setBasePath(other.getBasePath());
    setFactory(other.getFactory());
    setInsertOnly(other.getInsertOnly());
    setReturnsMutableVerticalTable(other.getReturnsMutableVerticalTable());
    setModifiableMutableVerticalTable(other.getModifiableMutableVerticalTable());
    setCompressed(other.getCompressed());
    setReferenceTable(other.getReferenceTable());
  }
  // by convention, always return *this
  return *this;
}

Loader::params *Loader::params::clone() const {
  Loader::params *p = new Loader::params();
  if (Input != nullptr) p->setInput(*Input);
  if (Header != nullptr) p->setHeader(*Header);
  p->setBasePath(BasePath);
  p->setFactory(Factory);
  p->setInsertOnly(InsertOnly);
  p->setReturnsMutableVerticalTable(ReturnsMutableVerticalTable);
  p->setModifiableMutableVerticalTable(ModifiableMutableVerticalTable);
  p->setReferenceTable(ReferenceTable);
  p->setCompressed(Compressed);
  return p;
}

Loader::params::~params() {
  if (Input != nullptr) delete Input;
  if (Input != nullptr) delete Header;
}


std::shared_ptr<AbstractTable> Loader::generateValidityTable(std::shared_ptr<AbstractTable> intable,
                                                             hyrise::tx::transaction_id_t tx) {
  size_t result_size = intable->size();
  std::vector<hyrise::storage::atable_ptr_t> tables { intable, hyrise::insertonly::validityTableForTransaction(result_size, tx) };
  return std::make_shared<MutableVerticalTable>(tables, result_size);
}

std::shared_ptr<AbstractTable> Loader::load(const params &args) {
  AbstractHeader *header = args.getHeader();
  AbstractTableFactory *factory = args.getFactory();
  AbstractInput *input = args.getInput();

  //TODO avoid leakage
  if (input == nullptr) {
    input = new EmptyInput();
  }
  if (header == nullptr) {
    header = new EmptyHeader();
  }
  if (factory == nullptr) {
    factory = new TableFactory<>();
  }

  LOG4CXX_DEBUG(logger, "Loading header");
  compound_metadata_list *meta = header->load(args);
  LOG4CXX_DEBUG(logger, "Header done");

  std::shared_ptr<AbstractTable>
  result, //initialize empty
  table = std::make_shared<MutableVerticalTable>(*meta, nullptr, 0, false, factory, args.getCompressed());

  LOG4CXX_DEBUG(logger, "Loading data");
  try {
    result = input->load(table, meta, args);
  } catch (std::exception& e) {
    // TODO: Memory management is not exception safe
    for (const auto & ml : *meta) {
      for (const auto & f : *ml)
        delete f;
      delete ml;
    }
    throw Loader::Error(e.what());
  }
  // Memory Cleanup, this is so dirty here, it makes me shiver
  for (auto e : *meta) {
    for (auto f: *e)
      delete f;
    delete e;
  }
  delete meta;

  LOG4CXX_DEBUG(logger, "Data done");

  if (!args.getModifiableMutableVerticalTable() && input->needs_store_wrap()) {
    std::shared_ptr<Store> s = std::make_shared<Store>(result);
    TableMerger *merger = new TableMerger(new LogarithmicMergeStrategy(0), new SequentialHeapMerger(), args.getCompressed());
    s->setMerger(merger);
    s->merge();
    result = s;
  }

  auto param = args.getInsertOnly();
  if (param.InsertOnly) {
    std::shared_ptr<AbstractTable> table = std::make_shared<hyrise::storage::SimpleStore>(generateValidityTable(result, param.transaction_id));
    return table;
  }
  
  if (!args.getModifiableMutableVerticalTable() && args.getReturnsMutableVerticalTable()) {
    table = std::dynamic_pointer_cast<Store>(result)->getMainTables()[0];
    result = table;
  }

  if (args.getHeader() == nullptr)
    delete header;

  if (args.getFactory() == nullptr)
    delete factory;

  if (args.getInput() == nullptr)
    delete input;

  return result;
}
