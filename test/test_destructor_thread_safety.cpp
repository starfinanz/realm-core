/*************************************************************************
 *
 * Copyright 2016 Realm Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 **************************************************************************/

#include "testsettings.hpp"
#ifdef TEST_DESTRUCTOR_THREAD_SAFETY

#include <realm.hpp>
#include <realm/util/features.h>
#include <memory>

#include "test.hpp"

using namespace realm;
using namespace realm::util;
using namespace realm::test_util;
using unit_test::TestContext;

// Tests thread safety of accessor chain manipulations related to LinkViews
TEST(ThreadSafety_LinkViewDestruction)
{
    std::vector<LinkViewRef> ptrs;
    Mutex mutex;
    Mutex destruct_mutex;
    test_util::ThreadWrapper thread;
    bool done = false;

    thread.start([&mutex, &destruct_mutex, &ptrs, &done] {
        while (true) {
            LockGuard lock(mutex);
            LockGuard lock1(destruct_mutex);
            ptrs.clear();
            if (done)
                break;
        }
    });

    for (int k = 0; k < 50; ++k) {
        auto group = std::make_shared<Group>();

        TableRef table = group->add_table("table");
        table->add_column(type_Int, "int");
        size_t col_link = table->add_column_link(type_LinkList, "links", *table);
        table->add_empty_row();
        table->add_empty_row();
        table->add_empty_row();
        {
            LinkViewRef links = table->get_linklist(col_link, 0);
            links->add(Key(2));
            links->add(Key(1));
            links->add(Key(0));
        }
        table->add_empty_row();
        for (int i = 0; i < 10000; i++) {
            LinkViewRef links = table->get_linklist(col_link, 0);
            {
                LockGuard lock(mutex);
                ptrs.push_back(links);
            }
        }
        {
            LockGuard lock(destruct_mutex);
            group.reset();
        }
    }
    {
        LockGuard lock(destruct_mutex);
        done = true;
    }
    thread.join();
}

// Tests thread safety of accessor chain manipulations related to TableViews
// (implies queries and descriptors). This test revealed a bug in the management
// of Descriptors.
TEST(ThreadSafety_TableViewDestruction)
{
    std::vector<std::shared_ptr<TableView>> ptrs;
    Mutex mutex;
    Mutex destruct_mutex;
    test_util::ThreadWrapper thread;
    bool done = false;

    thread.start([&mutex, &destruct_mutex, &ptrs, &done] {
        while (true) {
            LockGuard lock(mutex);
            LockGuard lock1(destruct_mutex);
            ptrs.clear();
            if (done)
                break;
        }
    });

    for (int k = 0; k < 20; ++k) {
        auto group = std::make_shared<Group>();

        TableRef table = group->add_table("table");
        table->add_column(type_Int, "int");
        for (int i = 0; i < 10000; i++) {
            auto table_view = std::make_shared<TableView>(table->where().find_all());
            {
                LockGuard lock(mutex);
                ptrs.push_back(table_view);
            }
        }
        {
            LockGuard lock(destruct_mutex);
            group.reset();
        }
    }
    {
        LockGuard lock(destruct_mutex);
        done = true;
    }
    thread.join();
}

// Tests thread safety of accessor chain manipulations related to Rows
TEST(ThreadSafety_RowDestruction)
{
    std::vector<Row> ptrs;
    Mutex mutex;
    Mutex destruct_mutex;
    test_util::ThreadWrapper thread;
    bool done = false;

    thread.start([&mutex, &destruct_mutex, &ptrs, &done] {
        while (true) {
            LockGuard lock(mutex);
            LockGuard lock1(destruct_mutex);
            ptrs.clear();
            if (done)
                break;
        }
    });

    for (int k = 0; k < 100; ++k) {
        auto group = std::make_shared<Group>();

        TableRef table = group->add_table("table");
        table->add_column(type_Int, "int");
        table->add_empty_row();
        for (int i = 0; i < 10000; i++) {
            Row r = table->get(0);
            {
                LockGuard lock(mutex);
                ptrs.push_back(r);
            }
        }
        {
            LockGuard lock(destruct_mutex);
            group.reset();
        }
    }
    {
        LockGuard lock(destruct_mutex);
        done = true;
    }
    thread.join();
}


#endif
