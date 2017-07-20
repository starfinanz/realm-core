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
#ifdef TEST_GROUP

#include <algorithm>
#include <fstream>

#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/types.h>
#endif

// File permissions for Windows
// http://stackoverflow.com/questions/592448/c-how-to-set-file-permissions-cross-platform
#ifdef _WIN32
#include <io.h>
typedef int mode_t2;
static const mode_t2 S_IWUSR = mode_t2(_S_IWRITE);
static const mode_t2 MS_MODE_MASK = 0x0000ffff;
#endif

#include <realm.hpp>
#include <realm/util/file.hpp>

#include "test.hpp"
#include "test_table_helper.hpp"

using namespace realm;
using namespace realm::util;
using namespace realm::test_util;

// Test independence and thread-safety
// -----------------------------------
//
// All tests must be thread safe and independent of each other. This
// is required because it allows for both shuffling of the execution
// order and for parallelized testing.
//
// In particular, avoid using std::rand() since it is not guaranteed
// to be thread safe. Instead use the API offered in
// `test/util/random.hpp`.
//
// All files created in tests must use the TEST_PATH macro (or one of
// its friends) to obtain a suitable file system path. See
// `test/util/test_path.hpp`.
//
//
// Debugging and the ONLY() macro
// ------------------------------
//
// A simple way of disabling all tests except one called `Foo`, is to
// replace TEST(Foo) with ONLY(Foo) and then recompile and rerun the
// test suite. Note that you can also use filtering by setting the
// environment varible `UNITTEST_FILTER`. See `README.md` for more on
// this.
//
// Another way to debug a particular test, is to copy that test into
// `experiments/testcase.cpp` and then run `sh build.sh
// check-testcase` (or one of its friends) from the command line.


namespace {

template <class T>
void test_table_add_columns(T t)
{
    t->add_column(type_String, "first");
    t->add_column(type_Int, "second");
    t->add_column(type_Bool, "third");
    t->add_column(type_Int, "fourth");
}

template <class T>
void setup_table(T t)
{
    add(t, "a", 1, true, Wed);
    add(t, "b", 15, true, Wed);
    add(t, "ccc", 10, true, Wed);
    add(t, "dddd", 20, true, Wed);
}

} // Anonymous namespace


TEST(Group_Unattached)
{
    Group group((Group::unattached_tag()));

    CHECK(!group.is_attached());
}


TEST(Group_UnattachedErrorHandling)
{
    Group group((Group::unattached_tag()));

    CHECK_EQUAL(false, group.is_empty());
    CHECK_EQUAL(0, group.size());
    CHECK_EQUAL(0, group.find_table("foo"));
    CHECK_LOGIC_ERROR(group.get_table(0), LogicError::detached_accessor);
    CHECK_LOGIC_ERROR(group.get_table("foo"), LogicError::detached_accessor);
    CHECK_LOGIC_ERROR(group.add_table("foo", false), LogicError::detached_accessor);
    CHECK_LOGIC_ERROR(group.insert_table(0, "foo", false), LogicError::detached_accessor);
    CHECK_LOGIC_ERROR(group.get_table(0), LogicError::detached_accessor);
    CHECK_LOGIC_ERROR(group.get_table("foo"), LogicError::detached_accessor);
    CHECK_LOGIC_ERROR(group.add_table("foo", false), LogicError::detached_accessor);
    CHECK_LOGIC_ERROR(group.insert_table(0, "foo", false), LogicError::detached_accessor);
    CHECK_LOGIC_ERROR(group.remove_table("foo"), LogicError::detached_accessor);
    CHECK_LOGIC_ERROR(group.remove_table(0), LogicError::detached_accessor);
    CHECK_LOGIC_ERROR(group.rename_table("foo", "bar", false), LogicError::detached_accessor);
    CHECK_LOGIC_ERROR(group.rename_table(0, "bar", false), LogicError::detached_accessor);
    CHECK_LOGIC_ERROR(group.move_table(0, 1), LogicError::detached_accessor);
    CHECK_LOGIC_ERROR(group.commit(), LogicError::detached_accessor);

    {
        const Group& const_group = group;
        CHECK_LOGIC_ERROR(const_group.get_table(0), LogicError::detached_accessor);
        CHECK_LOGIC_ERROR(const_group.get_table("foo"), LogicError::detached_accessor);
        CHECK_LOGIC_ERROR(const_group.get_table(0), LogicError::detached_accessor);
    }

    {
        bool f = false;
        CHECK_LOGIC_ERROR(group.get_or_add_table("foo", &f), LogicError::detached_accessor);
        CHECK_LOGIC_ERROR(group.get_or_insert_table(0, "foo", &f), LogicError::detached_accessor);
        CHECK_LOGIC_ERROR(group.get_or_add_table("foo", &f), LogicError::detached_accessor);
        CHECK_LOGIC_ERROR(group.get_or_insert_table(0, "foo", &f), LogicError::detached_accessor);
    }
    {
        std::ostringstream out;
        size_t link_depth = 0;
        std::map<std::string, std::string> renames;
        CHECK_LOGIC_ERROR(group.to_json(out, link_depth, &renames), LogicError::detached_accessor);
    }
}


TEST(Group_OpenFile)
{
    GROUP_TEST_PATH(path);

    {
        Group group((Group::unattached_tag()));
        group.open(path, crypt_key(), Group::mode_ReadWrite);
        CHECK(group.is_attached());
    }

    {
        Group group((Group::unattached_tag()));
        group.open(path, crypt_key(), Group::mode_ReadWriteNoCreate);
        CHECK(group.is_attached());
    }

    {
        Group group((Group::unattached_tag()));
        group.open(path, crypt_key(), Group::mode_ReadOnly);
        CHECK(group.is_attached());
    }
}

// Ensure that Group throws when you attempt to attach it twice in a row
TEST(Group_DoubleOpening)
{
    // File-based open()
    {
        GROUP_TEST_PATH(path);
        Group group((Group::unattached_tag()));

        group.open(path, crypt_key(), Group::mode_ReadWrite);
        CHECK_LOGIC_ERROR(group.open(path, crypt_key(), Group::mode_ReadWrite), LogicError::wrong_group_state);
    }

    // Buffer-based open()
    {
        // Produce a valid buffer
        using Deleter = decltype(::free)*;
        char* dummy = nullptr; // to make it compile on some early Visual Studio 2015 RC (now fixed properly in VC)
        std::unique_ptr<char[], Deleter> buffer(dummy, ::free);
        size_t buffer_size;

        {
            GROUP_TEST_PATH(path);
            {
                Group group;
                group.write(path);
            }
            {
                File file(path);
                buffer_size = size_t(file.get_size());
                buffer.reset(static_cast<char*>(malloc(buffer_size)));
                CHECK(bool(buffer));
                file.read(buffer.get(), buffer_size);
            }
        }

        Group group((Group::unattached_tag()));
        bool take_ownership = false;

        group.open(BinaryData(buffer.get(), buffer_size), take_ownership);
        CHECK_LOGIC_ERROR(group.open(BinaryData(buffer.get(), buffer_size), take_ownership),
                          LogicError::wrong_group_state);
    }
}

#if REALM_ENABLE_ENCRYPTION
TEST(Group_OpenUnencryptedFileWithKey)
{
    GROUP_TEST_PATH(path);
    {
        Group group(path, nullptr, Group::mode_ReadWrite);

        // We want the file to be exactly three pages in size so that trying to
        // read the footer would use the first non-zero field of the header as
        // the IV
        TableRef table = group.get_or_add_table("table");
        table->add_column(type_String, "str");
        std::string data(page_size() - 100, '\1');
        table->add_empty_row(2);
        table->set_string(0, 0, data);
        table->set_string(0, 1, data);

        group.commit();
    }

    {
        Group group((Group::unattached_tag()));
        CHECK_THROW(group.open(path, crypt_key(true), Group::mode_ReadWrite), InvalidDatabase);
    }
}
#endif // REALM_ENABLE_ENCRYPTION

#ifndef _WIN32
TEST(Group_Permissions)
{
    if (getuid() == 0) {
        std::cout << "Group_Permissions test skipped because you are running it as root\n\n";
        return;
    }

    GROUP_TEST_PATH(path);
    {
        Group group1;
        TableRef t1 = group1.add_table("table1");
        t1->add_column(type_String, "s");
        t1->add_column(type_Int, "i");
        for (size_t i = 0; i < 4; ++i) {
            t1->insert_empty_row(i);
            t1->set_string(0, i, "a");
            t1->set_int(1, i, 3);
        }
        group1.write(path, crypt_key());
    }

#ifdef _WIN32
    _chmod(path.c_str(), S_IWUSR & MS_MODE_MASK);
#else
    chmod(path.c_str(), S_IWUSR);
#endif

    {
        Group group2((Group::unattached_tag()));

        // Following two lines fail under Windows, fixme
        CHECK_THROW(group2.open(path, crypt_key(), Group::mode_ReadOnly), File::PermissionDenied);
        CHECK(!group2.is_attached());
    }
}
#endif


TEST(Group_BadFile)
{
    GROUP_TEST_PATH(path_1);
    GROUP_TEST_PATH(path_2);

    {
        File file(path_1, File::mode_Append);
        file.write("foo");
    }

    {
        Group group((Group::unattached_tag()));
        CHECK_THROW(group.open(path_1, crypt_key(), Group::mode_ReadOnly), InvalidDatabase);
        CHECK(!group.is_attached());
        CHECK_THROW(group.open(path_1, crypt_key(), Group::mode_ReadOnly), InvalidDatabase); // Again
        CHECK(!group.is_attached());
        CHECK_THROW(group.open(path_1, crypt_key(), Group::mode_ReadWrite), InvalidDatabase);
        CHECK(!group.is_attached());
        CHECK_THROW(group.open(path_1, crypt_key(), Group::mode_ReadWriteNoCreate), InvalidDatabase);
        CHECK(!group.is_attached());
        group.open(path_2, crypt_key(), Group::mode_ReadWrite); // This one must work
        CHECK(group.is_attached());
    }
}


TEST(Group_OpenBuffer)
{
    // Produce a valid buffer
    std::unique_ptr<char[]> buffer;
    size_t buffer_size;
    {
        GROUP_TEST_PATH(path);
        {
            Group group;
            group.write(path);
        }
        {
            File file(path);
            buffer_size = size_t(file.get_size());
            buffer.reset(static_cast<char*>(malloc(buffer_size)));
            CHECK(bool(buffer));
            file.read(buffer.get(), buffer_size);
        }
    }

    // Keep ownership of buffer
    {
        Group group((Group::unattached_tag()));
        bool take_ownership = false;
        group.open(BinaryData(buffer.get(), buffer_size), take_ownership);
        CHECK(group.is_attached());
    }

    // Pass ownership of buffer
    {
        Group group((Group::unattached_tag()));
        bool take_ownership = true;
        group.open(BinaryData(buffer.get(), buffer_size), take_ownership);
        CHECK(group.is_attached());
        buffer.release();
    }
}


TEST(Group_BadBuffer)
{
    GROUP_TEST_PATH(path);

    // Produce an invalid buffer
    char buffer[32];
    for (size_t i = 0; i < sizeof buffer; ++i)
        buffer[i] = char((i + 192) % 128);

    {
        Group group((Group::unattached_tag()));
        bool take_ownership = false;
        CHECK_THROW(group.open(BinaryData(buffer), take_ownership), InvalidDatabase);
        CHECK(!group.is_attached());
        // Check that ownership is not passed on failure during
        // open. If it was, we would get a bad delete on stack
        // allocated memory wich would at least be caught by Valgrind.
        take_ownership = true;
        CHECK_THROW(group.open(BinaryData(buffer), take_ownership), InvalidDatabase);
        CHECK(!group.is_attached());
        // Check that the group is still able to attach to a file,
        // even after failures.
        group.open(path, crypt_key(), Group::mode_ReadWrite);
        CHECK(group.is_attached());
    }
}


TEST(Group_Size)
{
    Group group;
    CHECK(group.is_attached());
    CHECK(group.is_empty());

    group.add_table("a");
    CHECK_NOT(group.is_empty());
    CHECK_EQUAL(1, group.size());

    group.add_table("b");
    CHECK_NOT(group.is_empty());
    CHECK_EQUAL(2, group.size());
}


TEST(Group_AddTable)
{
    Group group;
    TableRef foo_1 = group.add_table("foo");
    CHECK_EQUAL(1, group.size());
    CHECK_THROW(group.add_table("foo"), TableNameInUse);
    CHECK_EQUAL(1, group.size());
    bool require_unique_name = false;
    TableRef foo_2 = group.add_table("foo", require_unique_name);
    CHECK_EQUAL(2, group.size());
    CHECK_NOT_EQUAL(foo_1, foo_2);
}


TEST(Group_InsertTable)
{
    Group group;
    TableRef a = group.add_table("a");
    TableRef b = group.insert_table(0, "b");
    CHECK_EQUAL(2, group.size());
    CHECK_THROW(group.insert_table(2, "b"), TableNameInUse);
    CHECK_EQUAL(2, group.size());
    CHECK_EQUAL(a->get_index_in_group(), 1);
    CHECK_EQUAL(b->get_index_in_group(), 0);
}


TEST(Group_InsertTableWithLinks)
{
    using df = _impl::DescriptorFriend;

    Group group;
    TableRef a = group.add_table("a");
    TableRef b = group.add_table("b");
    a->add_column(type_Int, "foo");
    b->add_column_link(type_Link, "bar", *a);

    auto& a_spec = df::get_spec(*a->get_descriptor());
    auto& b_spec = df::get_spec(*b->get_descriptor());
    CHECK_EQUAL(b_spec.get_opposite_link_table_ndx(0), 0);
    CHECK_EQUAL(a_spec.get_opposite_link_table_ndx(1), 1);

    group.insert_table(0, "c");

    CHECK_EQUAL(b_spec.get_opposite_link_table_ndx(0), 1);
    CHECK_EQUAL(a_spec.get_opposite_link_table_ndx(1), 2);
}


TEST(Group_TableNameTooLong)
{
    Group group;
    size_t buf_len = 64;
    std::unique_ptr<char[]> buf(new char[buf_len]);
    CHECK_LOGIC_ERROR(group.add_table(StringData(buf.get(), buf_len)), LogicError::table_name_too_long);
    group.add_table(StringData(buf.get(), buf_len - 1));
}


TEST(Group_TableIndex)
{
    Group group;
    TableRef moja = group.add_table("moja");
    TableRef mbili = group.add_table("mbili");
    TableRef tatu = group.add_table("tatu");
    CHECK_EQUAL(3, group.size());
    std::vector<size_t> indexes;
    indexes.push_back(moja->get_index_in_group());
    indexes.push_back(mbili->get_index_in_group());
    indexes.push_back(tatu->get_index_in_group());
    sort(indexes.begin(), indexes.end());
    CHECK_EQUAL(0, indexes[0]);
    CHECK_EQUAL(1, indexes[1]);
    CHECK_EQUAL(2, indexes[2]);
    CHECK_EQUAL(moja, group.get_table(moja->get_index_in_group()));
    CHECK_EQUAL(mbili, group.get_table(mbili->get_index_in_group()));
    CHECK_EQUAL(tatu, group.get_table(tatu->get_index_in_group()));
    CHECK_EQUAL("moja", group.get_table_name(moja->get_index_in_group()));
    CHECK_EQUAL("mbili", group.get_table_name(mbili->get_index_in_group()));
    CHECK_EQUAL("tatu", group.get_table_name(tatu->get_index_in_group()));
    CHECK_LOGIC_ERROR(group.get_table(4), LogicError::table_index_out_of_range);
    CHECK_LOGIC_ERROR(group.get_table_name(4), LogicError::table_index_out_of_range);
}


TEST(Group_GetTable)
{
    Group group;
    const Group& cgroup = group;

    TableRef table_1 = group.add_table("table_1");
    TableRef table_2 = group.add_table("table_2");

    CHECK_NOT(group.get_table("foo"));
    CHECK_NOT(cgroup.get_table("foo"));
    CHECK_EQUAL(table_1, group.get_table("table_1"));
    CHECK_EQUAL(table_1, cgroup.get_table("table_1"));
    CHECK_EQUAL(table_2, group.get_table("table_2"));
    CHECK_EQUAL(table_2, cgroup.get_table("table_2"));
}


TEST(Group_GetOrAddTable)
{
    Group group;
    CHECK_EQUAL(0, group.size());
    group.get_or_add_table("a");
    CHECK_EQUAL(1, group.size());
    group.get_or_add_table("a");
    CHECK_EQUAL(1, group.size());

    bool was_created = false;
    group.get_or_add_table("foo", &was_created);
    CHECK(was_created);
    CHECK_EQUAL(2, group.size());
    group.get_or_add_table("foo", &was_created);
    CHECK_NOT(was_created);
    CHECK_EQUAL(2, group.size());
    group.get_or_add_table("bar", &was_created);
    CHECK(was_created);
    CHECK_EQUAL(3, group.size());
    group.get_or_add_table("baz", &was_created);
    CHECK(was_created);
    CHECK_EQUAL(4, group.size());
    group.get_or_add_table("bar", &was_created);
    CHECK_NOT(was_created);
    CHECK_EQUAL(4, group.size());
    group.get_or_add_table("baz", &was_created);
    CHECK_NOT(was_created);
    CHECK_EQUAL(4, group.size());
}


TEST(Group_GetOrInsertTable)
{
    Group group;
    bool was_inserted;
    group.get_or_insert_table(0, "foo", &was_inserted);
    CHECK_EQUAL(1, group.size());
    CHECK(was_inserted);
    group.get_or_insert_table(0, "foo", &was_inserted);
    CHECK_EQUAL(1, group.size());
    CHECK_NOT(was_inserted);
    group.get_or_insert_table(1, "foo", &was_inserted);
    CHECK_EQUAL(1, group.size());
    CHECK_NOT(was_inserted);
}


TEST(Group_BasicRemoveTable)
{
    Group group;
    TableRef alpha = group.add_table("alpha");
    TableRef beta = group.add_table("beta");
    TableRef gamma = group.add_table("gamma");
    TableRef delta = group.add_table("delta");
    CHECK_EQUAL(4, group.size());
    group.remove_table(gamma->get_index_in_group()); // By index
    CHECK_EQUAL(3, group.size());
    CHECK(alpha->is_attached());
    CHECK(beta->is_attached());
    CHECK_NOT(gamma->is_attached());
    CHECK(delta->is_attached());
    CHECK_EQUAL("alpha", group.get_table_name(alpha->get_index_in_group()));
    CHECK_EQUAL("beta", group.get_table_name(beta->get_index_in_group()));
    CHECK_EQUAL("delta", group.get_table_name(delta->get_index_in_group()));
    group.remove_table(alpha->get_index_in_group()); // By index
    CHECK_EQUAL(2, group.size());
    CHECK_NOT(alpha->is_attached());
    CHECK(beta->is_attached());
    CHECK_NOT(gamma->is_attached());
    CHECK(delta->is_attached());
    CHECK_EQUAL("beta", group.get_table_name(beta->get_index_in_group()));
    CHECK_EQUAL("delta", group.get_table_name(delta->get_index_in_group()));
    group.remove_table("delta"); // By name
    CHECK_EQUAL(1, group.size());
    CHECK_NOT(alpha->is_attached());
    CHECK(beta->is_attached());
    CHECK_NOT(gamma->is_attached());
    CHECK_NOT(delta->is_attached());
    CHECK_EQUAL("beta", group.get_table_name(beta->get_index_in_group()));
    CHECK_LOGIC_ERROR(group.remove_table(1), LogicError::table_index_out_of_range);
    CHECK_THROW(group.remove_table("epsilon"), NoSuchTable);
    group.verify();
}


TEST(Group_RemoveTableWithColumns)
{
    Group group;

    TableRef alpha = group.add_table("alpha");
    TableRef beta = group.add_table("beta");
    TableRef gamma = group.add_table("gamma");
    TableRef delta = group.add_table("delta");
    TableRef epsilon = group.add_table("epsilon");
    CHECK_EQUAL(5, group.size());

    alpha->add_column(type_Int, "alpha-1");
    beta->add_column_link(type_Link, "beta-1", *delta);
    gamma->add_column_link(type_Link, "gamma-1", *gamma);
    delta->add_column(type_Int, "delta-1");
    epsilon->add_column_link(type_Link, "epsilon-1", *delta);

    // Remove table with columns, but no link columns, and table is not a link
    // target.
    group.remove_table("alpha");
    CHECK_EQUAL(4, group.size());
    CHECK_NOT(alpha->is_attached());
    CHECK(beta->is_attached());
    CHECK(gamma->is_attached());
    CHECK(delta->is_attached());
    CHECK(epsilon->is_attached());

    // Remove table with link column, and table is not a link target.
    group.remove_table("beta");
    CHECK_EQUAL(3, group.size());
    CHECK_NOT(beta->is_attached());
    CHECK(gamma->is_attached());
    CHECK(delta->is_attached());
    CHECK(epsilon->is_attached());

    // Remove table with self-link column, and table is not a target of link
    // columns of other tables.
    group.remove_table("gamma");
    CHECK_EQUAL(2, group.size());
    CHECK_NOT(gamma->is_attached());
    CHECK(delta->is_attached());
    CHECK(epsilon->is_attached());

    // Try, but fail to remove table which is a target of link columns of other
    // tables.
    CHECK_THROW(group.remove_table("delta"), CrossTableLinkTarget);
    CHECK_EQUAL(2, group.size());
    CHECK(delta->is_attached());
    CHECK(epsilon->is_attached());
}


TEST(Group_RemoveTableMovesTableWithLinksOver)
{
    // Create a scenario where a table is removed from the group, and the last
    // table in the group (which will be moved into the vacated slot) has both
    // link and backlink columns.

    Group group;
    group.add_table("alpha");
    group.add_table("beta");
    group.add_table("gamma");
    group.add_table("delta");
    TableRef first = group.get_table(0);
    TableRef second = group.get_table(1);
    TableRef third = group.get_table(2);
    TableRef fourth = group.get_table(3);

    first->add_column_link(type_Link, "one", *third);
    third->add_column_link(type_Link, "two", *fourth);
    third->add_column_link(type_Link, "three", *third);
    fourth->add_column_link(type_Link, "four", *first);
    fourth->add_column_link(type_Link, "five", *third);
    first->add_empty_row(2);
    third->add_empty_row(2);
    fourth->add_empty_row(2);
    first->set_link(0, 0, 0);  // first[0].one   = third[0]
    first->set_link(0, 1, 1);  // first[1].one   = third[1]
    third->set_link(0, 0, 1);  // third[0].two   = fourth[1]
    third->set_link(0, 1, 0);  // third[1].two   = fourth[0]
    third->set_link(1, 0, 1);  // third[0].three = third[1]
    third->set_link(1, 1, 1);  // third[1].three = third[1]
    fourth->set_link(0, 0, 0); // fourth[0].four = first[0]
    fourth->set_link(0, 1, 0); // fourth[1].four = first[0]
    fourth->set_link(1, 0, 0); // fourth[0].five = third[0]
    fourth->set_link(1, 1, 1); // fourth[1].five = third[1]

    group.verify();

    group.remove_table(1); // Second

    group.verify();

    CHECK_EQUAL(3, group.size());
    CHECK(first->is_attached());
    CHECK_NOT(second->is_attached());
    CHECK(third->is_attached());
    CHECK(fourth->is_attached());
    CHECK_EQUAL(1, first->get_column_count());
    CHECK_EQUAL("one", first->get_column_name(0));
    CHECK_EQUAL(third, first->get_link_target(0));
    CHECK_EQUAL(2, third->get_column_count());
    CHECK_EQUAL("two", third->get_column_name(0));
    CHECK_EQUAL("three", third->get_column_name(1));
    CHECK_EQUAL(fourth, third->get_link_target(0));
    CHECK_EQUAL(third, third->get_link_target(1));
    CHECK_EQUAL(2, fourth->get_column_count());
    CHECK_EQUAL("four", fourth->get_column_name(0));
    CHECK_EQUAL("five", fourth->get_column_name(1));
    CHECK_EQUAL(first, fourth->get_link_target(0));
    CHECK_EQUAL(third, fourth->get_link_target(1));

    third->set_link(0, 0, 0);  // third[0].two   = fourth[0]
    fourth->set_link(0, 1, 1); // fourth[1].four = first[1]
    first->set_link(0, 0, 1);  // first[0].one   = third[1]

    group.verify();

    CHECK_EQUAL(2, first->size());
    CHECK_EQUAL(1, first->get_link(0, 0));
    CHECK_EQUAL(1, first->get_link(0, 1));
    CHECK_EQUAL(1, first->get_backlink_count(0, *fourth, 0));
    CHECK_EQUAL(1, first->get_backlink_count(1, *fourth, 0));
    CHECK_EQUAL(2, third->size());
    CHECK_EQUAL(0, third->get_link(0, 0));
    CHECK_EQUAL(0, third->get_link(0, 1));
    CHECK_EQUAL(1, third->get_link(1, 0));
    CHECK_EQUAL(1, third->get_link(1, 1));
    CHECK_EQUAL(0, third->get_backlink_count(0, *first, 0));
    CHECK_EQUAL(2, third->get_backlink_count(1, *first, 0));
    CHECK_EQUAL(0, third->get_backlink_count(0, *third, 1));
    CHECK_EQUAL(2, third->get_backlink_count(1, *third, 1));
    CHECK_EQUAL(1, third->get_backlink_count(0, *fourth, 1));
    CHECK_EQUAL(1, third->get_backlink_count(1, *fourth, 1));
    CHECK_EQUAL(2, fourth->size());
    CHECK_EQUAL(0, fourth->get_link(0, 0));
    CHECK_EQUAL(1, fourth->get_link(0, 1));
    CHECK_EQUAL(0, fourth->get_link(1, 0));
    CHECK_EQUAL(1, fourth->get_link(1, 1));
    CHECK_EQUAL(2, fourth->get_backlink_count(0, *third, 0));
    CHECK_EQUAL(0, fourth->get_backlink_count(1, *third, 0));
}


TEST(Group_RemoveLinkTable)
{
    Group group;
    TableRef table = group.add_table("table");
    table->add_column_link(type_Link, "", *table);
    group.remove_table(table->get_index_in_group());
    CHECK(group.is_empty());
    CHECK(!table->is_attached());
    TableRef origin = group.add_table("origin");
    TableRef target = group.add_table("target");
    target->add_column(type_Int, "");
    origin->add_column_link(type_Link, "", *target);
    CHECK_THROW(group.remove_table(target->get_index_in_group()), CrossTableLinkTarget);
    group.remove_table(origin->get_index_in_group());
    CHECK_EQUAL(1, group.size());
    CHECK(!origin->is_attached());
    CHECK(target->is_attached());
    group.verify();
}


TEST(Group_RenameTable)
{
    Group group;
    TableRef alpha = group.add_table("alpha");
    TableRef beta = group.add_table("beta");
    TableRef gamma = group.add_table("gamma");
    group.rename_table(beta->get_index_in_group(), "delta");
    CHECK_EQUAL("delta", beta->get_name());
    group.rename_table("delta", "epsilon");
    CHECK_EQUAL("alpha", alpha->get_name());
    CHECK_EQUAL("epsilon", beta->get_name());
    CHECK_EQUAL("gamma", gamma->get_name());
    CHECK_LOGIC_ERROR(group.rename_table(3, "zeta"), LogicError::table_index_out_of_range);
    CHECK_THROW(group.rename_table("eta", "theta"), NoSuchTable);
    CHECK_THROW(group.rename_table("epsilon", "alpha"), TableNameInUse);
    bool require_unique_name = false;
    group.rename_table("epsilon", "alpha", require_unique_name);
    CHECK_EQUAL("alpha", alpha->get_name());
    CHECK_EQUAL("alpha", beta->get_name());
    CHECK_EQUAL("gamma", gamma->get_name());
    group.verify();
}


TEST(Group_BasicMoveTable)
{
    Group group;
    TableRef alpha = group.add_table("alpha");
    TableRef beta = group.add_table("beta");
    TableRef gamma = group.add_table("gamma");
    TableRef delta = group.add_table("delta");
    CHECK_EQUAL(4, group.size());

    // Move up:
    group.move_table(1, 3);
    CHECK_EQUAL(4, group.size());
    CHECK(alpha->is_attached());
    CHECK(beta->is_attached());
    CHECK(gamma->is_attached());
    CHECK(delta->is_attached());
    CHECK_EQUAL(0, alpha->get_index_in_group());
    CHECK_EQUAL(3, beta->get_index_in_group());
    CHECK_EQUAL(1, gamma->get_index_in_group());
    CHECK_EQUAL(2, delta->get_index_in_group());

    group.verify();

    // Move down:
    group.move_table(2, 0);
    CHECK_EQUAL(4, group.size());
    CHECK(alpha->is_attached());
    CHECK(beta->is_attached());
    CHECK(gamma->is_attached());
    CHECK(delta->is_attached());
    CHECK_EQUAL(1, alpha->get_index_in_group());
    CHECK_EQUAL(3, beta->get_index_in_group());
    CHECK_EQUAL(2, gamma->get_index_in_group());
    CHECK_EQUAL(0, delta->get_index_in_group());

    group.verify();
}

TEST(Group_MoveTableWithLinks)
{
    using df = _impl::DescriptorFriend;
    Group group;
    TableRef a = group.add_table("a");
    TableRef b = group.add_table("b");
    TableRef c = group.add_table("c");
    TableRef d = group.add_table("d");
    CHECK_EQUAL(4, group.size());
    a->add_column_link(type_Link, "link_to_b", *b);
    b->add_column_link(type_LinkList, "link_to_c", *c);
    c->add_column_link(type_Link, "link_to_d", *d);
    d->add_column_link(type_LinkList, "link_to_a", *a);

    auto& a_spec = df::get_spec(*a->get_descriptor());
    auto& b_spec = df::get_spec(*b->get_descriptor());
    auto& c_spec = df::get_spec(*c->get_descriptor());
    auto& d_spec = df::get_spec(*d->get_descriptor());

    // Move up:
    group.move_table(1, 3);
    CHECK(a->is_attached());
    CHECK(b->is_attached());
    CHECK(c->is_attached());
    CHECK(d->is_attached());
    CHECK_EQUAL(a->get_link_target(0), b);
    CHECK_EQUAL(b->get_link_target(0), c);
    CHECK_EQUAL(c->get_link_target(0), d);
    CHECK_EQUAL(d->get_link_target(0), a);
    // Check backlink columns
    CHECK_EQUAL(a_spec.get_opposite_link_table_ndx(1), d->get_index_in_group());
    CHECK_EQUAL(b_spec.get_opposite_link_table_ndx(1), a->get_index_in_group());
    CHECK_EQUAL(c_spec.get_opposite_link_table_ndx(1), b->get_index_in_group());
    CHECK_EQUAL(d_spec.get_opposite_link_table_ndx(1), c->get_index_in_group());

    // Move down:
    group.move_table(2, 0);
    CHECK(a->is_attached());
    CHECK(b->is_attached());
    CHECK(c->is_attached());
    CHECK(d->is_attached());
    CHECK_EQUAL(a->get_link_target(0), b);
    CHECK_EQUAL(b->get_link_target(0), c);
    CHECK_EQUAL(c->get_link_target(0), d);
    CHECK_EQUAL(d->get_link_target(0), a);
    // Check backlink columns
    CHECK_EQUAL(a_spec.get_opposite_link_table_ndx(1), d->get_index_in_group());
    CHECK_EQUAL(b_spec.get_opposite_link_table_ndx(1), a->get_index_in_group());
    CHECK_EQUAL(c_spec.get_opposite_link_table_ndx(1), b->get_index_in_group());
    CHECK_EQUAL(d_spec.get_opposite_link_table_ndx(1), c->get_index_in_group());
}


TEST(Group_MoveTableImmediatelyAfterOpen)
{
    Group g1;
    TableRef a = g1.add_table("a");
    TableRef b = g1.add_table("b");
    CHECK_EQUAL(2, g1.size());

    Group g2(g1.write_to_mem());
    g2.move_table(0, 1);
    CHECK_EQUAL(2, g2.size());
    CHECK_EQUAL("b", g2.get_table_name(0));
    CHECK_EQUAL("a", g2.get_table_name(1));
}


TEST(Group_Equal)
{
    Group g1, g2, g3;
    CHECK(g1 == g2);
    auto t1 = g1.add_table("TABLE1");
    test_table_add_columns(t1);
    CHECK_NOT(g1 == g2);
    setup_table(t1);
    auto t2 = g2.add_table("TABLE1");
    test_table_add_columns(t2);
    setup_table(t2);
    CHECK(g1 == g2);
    add(t2, "hey", 2, false, Thu);
    CHECK(g1 != g2);
    auto t3 = g3.add_table("TABLE3");
    test_table_add_columns(t3);
    setup_table(t3);
    CHECK(g1 != g3);
}


TEST(Group_TableAccessorLeftBehind)
{
    TableRef table;
    TableRef subtable;
    {
        Group group;
        table = group.add_table("test");
        CHECK(table->is_attached());
        table->add_column(type_Table, "sub");
        table->add_empty_row();
        subtable = table->get_subtable(0, 0);
        CHECK(subtable->is_attached());
    }
    CHECK(!table->is_attached());
    CHECK(!subtable->is_attached());
}


TEST(Group_SubtableDescriptors)
{
    // This test originally only failed when checked with valgrind as the
    // problem was that memory was read after being freed.
    GROUP_TEST_PATH(path);

    // Create new database
    Group g(path, crypt_key(), Group::mode_ReadWrite);

    TableRef table = g.add_table("first");
    {
        DescriptorRef subdescr;
        table->add_column(type_Table, "sub", false, &subdescr);
        subdescr->add_column(type_Int, "integers", nullptr, false);
    }
    table->add_empty_row(125);

    TableRef sub = table->get_subtable(0, 3);
    sub->clear();
    sub->add_empty_row(5);
    sub->set_int(0, 0, 127, false);
    sub->set_int(0, 1, 127, false);
    sub->set_int(0, 2, 255, false);
    sub->set_int(0, 3, 128, false);
    sub->set_int(0, 4, 4, false);

    // this will keep a subdescriptor alive during the commit
    int64_t val = sub->get_int(0, 2);
    TableView tv = sub->where().equal(0, val).find_all();

    table->get_subdescriptor(0)->add_search_index(0);
    g.commit();
    table->get_subdescriptor(0)->remove_search_index(0);
}

TEST(Group_UpdateSubtableDescriptorsAccessors)
{
    GROUP_TEST_PATH(path);
    Group g(path, crypt_key(), Group::mode_ReadWrite);

    TableRef table = g.add_table("first");

    {
        DescriptorRef subdescr;
        table->add_column(type_Table, "sub1", true, &subdescr);
        subdescr->add_column(type_Int, "integers", nullptr, false);
    }

    {
        DescriptorRef subdescr;
        table->add_column(type_Table, "sub2", true, &subdescr);
        subdescr->add_column(type_Int, "integers", nullptr, false);
    }

    g.commit();

    table->get_subdescriptor(1)->remove_search_index(0);
    table->remove_column(0);
    table->get_subdescriptor(0)->add_search_index(0);
}

TEST(Group_Invalid1)
{
    GROUP_TEST_PATH(path);

    // Try to open non-existing file
    // (read-only files have to exists to before opening)
    CHECK_THROW(Group(path, crypt_key()), File::NotFound);
}


TEST(Group_Invalid2)
{
    // Try to open buffer with invalid data
    const char* const str = "invalid data";
    CHECK_THROW(Group(BinaryData(str, strlen(str))), InvalidDatabase);
}


TEST(Group_Overwrite)
{
    GROUP_TEST_PATH(path);
    {
        Group g;
        g.write(path, crypt_key());
        CHECK_THROW(g.write(path, crypt_key()), File::Exists);
    }
    {
        Group g(path, crypt_key());
        CHECK_THROW(g.write(path, crypt_key()), File::Exists);
    }
    {
        Group g;
        File::try_remove(path);
        g.write(path, crypt_key());
    }
}


TEST(Group_Serialize0)
{
    GROUP_TEST_PATH(path);
    {
        // Create empty group and serialize to disk
        Group to_disk;
        to_disk.write(path, crypt_key());

        // Load the group
        Group from_disk(path, crypt_key());

        // Create new table in group
        auto t = from_disk.add_table("test");
        test_table_add_columns(t);

        CHECK_EQUAL(4, t->get_column_count());
        CHECK_EQUAL(0, t->size());

        // Modify table
        add(t, "Test", 1, true, Wed);

        CHECK_EQUAL("Test", t->get_string(0, 0));
        CHECK_EQUAL(1, t->get_int(1, 0));
        CHECK_EQUAL(true, t->get_bool(2, 0));
        CHECK_EQUAL(Wed, t->get_int(3, 0));
    }
    {
        // Load the group and let it clean up without loading
        // any tables
        Group g(path, crypt_key());
    }
}


TEST(Group_Serialize1)
{
    GROUP_TEST_PATH(path);
    {
        // Create group with one table
        Group to_disk;
        auto table = to_disk.add_table("test");
        test_table_add_columns(table);
        add(table, "", 1, true, Wed);
        add(table, "", 15, true, Wed);
        add(table, "", 10, true, Wed);
        add(table, "", 20, true, Wed);
        add(table, "", 11, true, Wed);
        add(table, "", 45, true, Wed);
        add(table, "", 10, true, Wed);
        add(table, "", 0, true, Wed);
        add(table, "", 30, true, Wed);
        add(table, "", 9, true, Wed);

#ifdef REALM_DEBUG
        to_disk.verify();
#endif

        // Serialize to disk
        to_disk.write(path, crypt_key());

        // Load the table
        Group from_disk(path, crypt_key());
        auto t = from_disk.get_table("test");

        CHECK_EQUAL(4, t->get_column_count());
        CHECK_EQUAL(10, t->size());

        // Verify that original values are there
        CHECK(*table == *t);

        // Modify both tables
        table->set_string(0, 0, "test");
        t->set_string(0, 0, "test");

        insert(table, 5, "hello", 100, false, Mon);
        insert(t, 5, "hello", 100, false, Mon);
        table->remove(1);
        t->remove(1);

        // Verify that both changed correctly
        CHECK(*table == *t);
#ifdef REALM_DEBUG
        to_disk.verify();
        from_disk.verify();
#endif
    }
    {
        // Load the group and let it clean up without loading
        // any tables
        Group g(path, crypt_key());
    }
}


TEST(Group_Serialize2)
{
    GROUP_TEST_PATH(path);

    // Create group with two tables
    Group to_disk;
    TableRef table1 = to_disk.add_table("test1");
    test_table_add_columns(table1);
    add(table1, "", 1, true, Wed);
    add(table1, "", 15, true, Wed);
    add(table1, "", 10, true, Wed);

    TableRef table2 = to_disk.add_table("test2");
    test_table_add_columns(table2);
    add(table2, "hey", 0, true, Tue);
    add(table2, "hello", 3232, false, Sun);

#ifdef REALM_DEBUG
    to_disk.verify();
#endif

    // Serialize to disk
    to_disk.write(path, crypt_key());

    // Load the tables
    Group from_disk(path, crypt_key());
    TableRef t1 = from_disk.get_table("test1");
    TableRef t2 = from_disk.get_table("test2");

    // Verify that original values are there
    CHECK(*table1 == *t1);
    CHECK(*table2 == *t2);

#ifdef REALM_DEBUG
    to_disk.verify();
    from_disk.verify();
#endif
}


TEST(Group_Serialize3)
{
    GROUP_TEST_PATH(path);

    // Create group with one table (including long strings
    Group to_disk;
    TableRef table = to_disk.add_table("test");
    test_table_add_columns(table);
    add(table, "1 xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx 1", 1, true, Wed);
    add(table, "2 xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx 2", 15, true, Wed);

#ifdef REALM_DEBUG
    to_disk.verify();
#endif

    // Serialize to disk
    to_disk.write(path, crypt_key());

    // Load the table
    Group from_disk(path, crypt_key());
    TableRef t = from_disk.get_table("test");

    // Verify that original values are there
    CHECK(*table == *t);
#ifdef REALM_DEBUG
    to_disk.verify();
    from_disk.verify();
#endif
}


TEST(Group_Serialize_Mem)
{
    // Create group with one table
    Group to_mem;
    TableRef table = to_mem.add_table("test");
    test_table_add_columns(table);
    add(table, "", 1, true, Wed);
    add(table, "", 15, true, Wed);
    add(table, "", 10, true, Wed);
    add(table, "", 20, true, Wed);
    add(table, "", 11, true, Wed);
    add(table, "", 45, true, Wed);
    add(table, "", 10, true, Wed);
    add(table, "", 0, true, Wed);
    add(table, "", 30, true, Wed);
    add(table, "", 9, true, Wed);

#ifdef REALM_DEBUG
    to_mem.verify();
#endif

    // Serialize to memory (we now own the buffer)
    BinaryData buffer = to_mem.write_to_mem();

    // Load the table
    Group from_mem(buffer);
    TableRef t = from_mem.get_table("test");

    CHECK_EQUAL(4, t->get_column_count());
    CHECK_EQUAL(10, t->size());

    // Verify that original values are there
    CHECK(*table == *t);
#ifdef REALM_DEBUG
    to_mem.verify();
    from_mem.verify();
#endif
}


TEST(Group_Close)
{
    Group to_mem;
    TableRef table = to_mem.add_table("test");
    test_table_add_columns(table);
    add(table, "", 1, true, Wed);
    add(table, "", 2, true, Wed);

    // Serialize to memory (we now own the buffer)
    BinaryData buffer = to_mem.write_to_mem();

    Group from_mem(buffer);
}


TEST(Group_Serialize_Optimized)
{
    // Create group with one table
    Group to_mem;
    TableRef table = to_mem.add_table("test");
    test_table_add_columns(table);

    for (size_t i = 0; i < 5; ++i) {
        add(table, "abd", 1, true, Mon);
        add(table, "eftg", 2, true, Tue);
        add(table, "hijkl", 5, true, Wed);
        add(table, "mnopqr", 8, true, Thu);
        add(table, "stuvxyz", 9, true, Fri);
    }

    table->optimize();

#ifdef REALM_DEBUG
    to_mem.verify();
#endif

    // Serialize to memory (we now own the buffer)
    BinaryData buffer = to_mem.write_to_mem();

    // Load the table
    Group from_mem(buffer);
    TableRef t = from_mem.get_table("test");

    CHECK_EQUAL(4, t->get_column_count());

    // Verify that original values are there
    CHECK(*table == *t);

    // Add a row with a known (but unique) value
    add(table, "search_target", 9, true, Fri);

    const size_t res = table->find_first_string(0, "search_target");
    CHECK_EQUAL(table->size() - 1, res);

#ifdef REALM_DEBUG
    to_mem.verify();
    from_mem.verify();
#endif
}


TEST(Group_Serialize_All)
{
    // Create group with one table
    Group to_mem;
    TableRef table = to_mem.add_table("test");

    table->add_column(type_Int, "int");
    table->add_column(type_Bool, "bool");
    table->add_column(type_OldDateTime, "date");
    table->add_column(type_String, "string");
    table->add_column(type_Binary, "binary");
    table->add_column(type_Mixed, "mixed");

    table->insert_empty_row(0);
    table->set_int(0, 0, 12);
    table->set_bool(1, 0, true);
    table->set_olddatetime(2, 0, 12345);
    table->set_string(3, 0, "test");
    table->set_binary(4, 0, BinaryData("binary", 7));
    table->set_mixed(5, 0, false);

    // Serialize to memory (we now own the buffer)
    BinaryData buffer = to_mem.write_to_mem();

    // Load the table
    Group from_mem(buffer);
    TableRef t = from_mem.get_table("test");

    CHECK_EQUAL(6, t->get_column_count());
    CHECK_EQUAL(1, t->size());
    CHECK_EQUAL(12, t->get_int(0, 0));
    CHECK_EQUAL(true, t->get_bool(1, 0));
    CHECK_EQUAL(12345, t->get_olddatetime(2, 0));
    CHECK_EQUAL("test", t->get_string(3, 0));
    CHECK_EQUAL(7, t->get_binary(4, 0).size());
    CHECK_EQUAL("binary", t->get_binary(4, 0).data());
    CHECK_EQUAL(type_Bool, t->get_mixed(5, 0).get_type());
    CHECK_EQUAL(false, t->get_mixed(5, 0).get_bool());
}

TEST(Group_Persist)
{
    GROUP_TEST_PATH(path);

    // Create new database
    Group db(path, crypt_key(), Group::mode_ReadWrite);

    // Insert some data
    TableRef table = db.add_table("test");
    table->add_column(type_Int, "int");
    table->add_column(type_Bool, "bool");
    table->add_column(type_OldDateTime, "date");
    table->add_column(type_String, "string");
    table->add_column(type_Binary, "binary");
    table->add_column(type_Mixed, "mixed");
    table->add_column(type_Timestamp, "timestamp");
    table->insert_empty_row(0);
    table->set_int(0, 0, 12);
    table->set_bool(1, 0, true);
    table->set_olddatetime(2, 0, 12345);
    table->set_string(3, 0, "test");
    table->set_binary(4, 0, BinaryData("binary", 7));
    table->set_mixed(5, 0, false);
    table->set_timestamp(6, 0, Timestamp(111, 222));

    // Write changes to file
    db.commit();

#ifdef REALM_DEBUG
    db.verify();
#endif

    CHECK_EQUAL(7, table->get_column_count());
    CHECK_EQUAL(1, table->size());
    CHECK_EQUAL(12, table->get_int(0, 0));
    CHECK_EQUAL(true, table->get_bool(1, 0));
    CHECK_EQUAL(12345, table->get_olddatetime(2, 0));
    CHECK_EQUAL("test", table->get_string(3, 0));
    CHECK_EQUAL(7, table->get_binary(4, 0).size());
    CHECK_EQUAL("binary", table->get_binary(4, 0).data());
    CHECK_EQUAL(type_Bool, table->get_mixed(5, 0).get_type());
    CHECK_EQUAL(false, table->get_mixed(5, 0).get_bool());
    CHECK(table->get_timestamp(6, 0) == Timestamp(111, 222));

    // Change a bit
    table->set_string(3, 0, "Changed!");

    // Write changes to file
    db.commit();

#ifdef REALM_DEBUG
    db.verify();
#endif

    CHECK_EQUAL(7, table->get_column_count());
    CHECK_EQUAL(1, table->size());
    CHECK_EQUAL(12, table->get_int(0, 0));
    CHECK_EQUAL(true, table->get_bool(1, 0));
    CHECK_EQUAL(12345, table->get_olddatetime(2, 0));
    CHECK_EQUAL("Changed!", table->get_string(3, 0));
    CHECK_EQUAL(7, table->get_binary(4, 0).size());
    CHECK_EQUAL("binary", table->get_binary(4, 0).data());
    CHECK_EQUAL(type_Bool, table->get_mixed(5, 0).get_type());
    CHECK_EQUAL(false, table->get_mixed(5, 0).get_bool());
    CHECK(table->get_timestamp(6, 0) == Timestamp(111, 222));
}


TEST(Group_Subtable)
{
    GROUP_TEST_PATH(path_1);
    GROUP_TEST_PATH(path_2);

    int n = 1;

    Group g;
    TableRef table = g.add_table("test");
    DescriptorRef sub;
    table->add_column(type_Int, "foo");
    table->add_column(type_Table, "sub", &sub);
    table->add_column(type_Mixed, "baz");
    sub->add_column(type_Int, "bar");
    sub.reset();

    for (int i = 0; i < n; ++i) {
        table->add_empty_row();
        table->set_int(0, i, 100 + i);
        if (i % 2 == 0) {
            TableRef st = table->get_subtable(1, i);
            st->add_empty_row();
            st->set_int(0, 0, 200 + i);
        }
        if (i % 3 == 1) {
            table->set_mixed(2, i, Mixed::subtable_tag());
            TableRef st = table->get_subtable(2, i);
            st->add_column(type_Int, "banach");
            st->add_empty_row();
            st->set_int(0, 0, 700 + i);
        }
    }

    CHECK_EQUAL(n, table->size());

    for (int i = 0; i < n; ++i) {
        CHECK_EQUAL(100 + i, table->get_int(0, i));
        {
            TableRef st = table->get_subtable(1, i);
            CHECK_EQUAL(i % 2 == 0 ? 1 : 0, st->size());
            if (i % 2 == 0)
                CHECK_EQUAL(200 + i, st->get_int(0, 0));
            if (i % 3 == 0) {
                st->add_empty_row();
                st->set_int(0, st->size() - 1, 300 + i);
            }
        }
        CHECK_EQUAL(i % 3 == 1 ? type_Table : type_Int, table->get_mixed_type(2, i));
        if (i % 3 == 1) {
            TableRef st = table->get_subtable(2, i);
            CHECK_EQUAL(1, st->size());
            CHECK_EQUAL(700 + i, st->get_int(0, 0));
        }
        if (i % 8 == 3) {
            if (i % 3 != 1)
                table->set_mixed(2, i, Mixed::subtable_tag());
            TableRef st = table->get_subtable(2, i);
            if (i % 3 != 1)
                st->add_column(type_Int, "banach");
            st->add_empty_row();
            st->set_int(0, st->size() - 1, 800 + i);
        }
    }

    for (int i = 0; i < n; ++i) {
        CHECK_EQUAL(100 + i, table->get_int(0, i));
        {
            TableRef st = table->get_subtable(1, i);
            size_t expected_size = (i % 2 == 0 ? 1 : 0) + (i % 3 == 0 ? 1 : 0);
            CHECK_EQUAL(expected_size, st->size());
            size_t ndx = 0;
            if (i % 2 == 0) {
                CHECK_EQUAL(200 + i, st->get_int(0, ndx));
                ++ndx;
            }
            if (i % 3 == 0) {
                CHECK_EQUAL(300 + i, st->get_int(0, ndx));
                ++ndx;
            }
        }
        CHECK_EQUAL(i % 3 == 1 || i % 8 == 3 ? type_Table : type_Int, table->get_mixed_type(2, i));
        if (i % 3 == 1 || i % 8 == 3) {
            TableRef st = table->get_subtable(2, i);
            size_t expected_size = (i % 3 == 1 ? 1 : 0) + (i % 8 == 3 ? 1 : 0);
            CHECK_EQUAL(expected_size, st->size());
            size_t ndx = 0;
            if (i % 3 == 1) {
                CHECK_EQUAL(700 + i, st->get_int(0, ndx));
                ++ndx;
            }
            if (i % 8 == 3) {
                CHECK_EQUAL(800 + i, st->get_int(0, ndx));
                ++ndx;
            }
        }
    }

    g.write(path_1, crypt_key());

    // Read back tables
    Group g2(path_1, crypt_key());
    TableRef table2 = g2.get_table("test");

    for (int i = 0; i < n; ++i) {
        CHECK_EQUAL(100 + i, table2->get_int(0, i));
        {
            TableRef st = table2->get_subtable(1, i);
            size_t expected_size = (i % 2 == 0 ? 1 : 0) + (i % 3 == 0 ? 1 : 0);
            CHECK_EQUAL(expected_size, st->size());
            size_t ndx = 0;
            if (i % 2 == 0) {
                CHECK_EQUAL(200 + i, st->get_int(0, ndx));
                ++ndx;
            }
            if (i % 3 == 0) {
                CHECK_EQUAL(300 + i, st->get_int(0, ndx));
                ++ndx;
            }
            if (i % 5 == 0) {
                st->add_empty_row();
                st->set_int(0, st->size() - 1, 400 + i);
            }
        }
        CHECK_EQUAL(i % 3 == 1 || i % 8 == 3 ? type_Table : type_Int, table2->get_mixed_type(2, i));
        if (i % 3 == 1 || i % 8 == 3) {
            TableRef st = table2->get_subtable(2, i);
            size_t expected_size = (i % 3 == 1 ? 1 : 0) + (i % 8 == 3 ? 1 : 0);
            CHECK_EQUAL(expected_size, st->size());
            size_t ndx = 0;
            if (i % 3 == 1) {
                CHECK_EQUAL(700 + i, st->get_int(0, ndx));
                ++ndx;
            }
            if (i % 8 == 3) {
                CHECK_EQUAL(800 + i, st->get_int(0, ndx));
                ++ndx;
            }
        }
        if (i % 7 == 4) {
            if (i % 3 != 1 && i % 8 != 3)
                table2->set_mixed(2, i, Mixed::subtable_tag());
            TableRef st = table2->get_subtable(2, i);
            if (i % 3 != 1 && i % 8 != 3)
                st->add_column(type_Int, "banach");
            st->add_empty_row();
            st->set_int(0, st->size() - 1, 900 + i);
        }
    }

    for (int i = 0; i < n; ++i) {
        CHECK_EQUAL(100 + i, table2->get_int(0, i));
        {
            TableRef st = table2->get_subtable(1, i);
            size_t expected_size = (i % 2 == 0 ? 1 : 0) + (i % 3 == 0 ? 1 : 0) + (i % 5 == 0 ? 1 : 0);
            CHECK_EQUAL(expected_size, st->size());
            size_t ndx = 0;
            if (i % 2 == 0) {
                CHECK_EQUAL(200 + i, st->get_int(0, ndx));
                ++ndx;
            }
            if (i % 3 == 0) {
                CHECK_EQUAL(300 + i, st->get_int(0, ndx));
                ++ndx;
            }
            if (i % 5 == 0) {
                CHECK_EQUAL(400 + i, st->get_int(0, ndx));
                ++ndx;
            }
        }
        CHECK_EQUAL(i % 3 == 1 || i % 8 == 3 || i % 7 == 4 ? type_Table : type_Int, table2->get_mixed_type(2, i));
        if (i % 3 == 1 || i % 8 == 3 || i % 7 == 4) {
            TableRef st = table2->get_subtable(2, i);
            size_t expected_size = (i % 3 == 1 ? 1 : 0) + (i % 8 == 3 ? 1 : 0) + (i % 7 == 4 ? 1 : 0);
            CHECK_EQUAL(expected_size, st->size());
            size_t ndx = 0;
            if (i % 3 == 1) {
                CHECK_EQUAL(700 + i, st->get_int(0, ndx));
                ++ndx;
            }
            if (i % 8 == 3) {
                CHECK_EQUAL(800 + i, st->get_int(0, ndx));
                ++ndx;
            }
            if (i % 7 == 4) {
                CHECK_EQUAL(900 + i, st->get_int(0, ndx));
                ++ndx;
            }
        }
    }

    g2.write(path_2, crypt_key());

    // Read back tables
    Group g3(path_2, crypt_key());
    TableRef table3 = g2.get_table("test");

    for (int i = 0; i < n; ++i) {
        CHECK_EQUAL(100 + i, table3->get_int(0, i));
        {
            TableRef st = table3->get_subtable(1, i);
            size_t expected_size = (i % 2 == 0 ? 1 : 0) + (i % 3 == 0 ? 1 : 0) + (i % 5 == 0 ? 1 : 0);
            CHECK_EQUAL(expected_size, st->size());
            size_t ndx = 0;
            if (i % 2 == 0) {
                CHECK_EQUAL(200 + i, st->get_int(0, ndx));
                ++ndx;
            }
            if (i % 3 == 0) {
                CHECK_EQUAL(300 + i, st->get_int(0, ndx));
                ++ndx;
            }
            if (i % 5 == 0) {
                CHECK_EQUAL(400 + i, st->get_int(0, ndx));
                ++ndx;
            }
        }
        CHECK_EQUAL(i % 3 == 1 || i % 8 == 3 || i % 7 == 4 ? type_Table : type_Int, table3->get_mixed_type(2, i));
        if (i % 3 == 1 || i % 8 == 3 || i % 7 == 4) {
            TableRef st = table3->get_subtable(2, i);
            size_t expected_size = (i % 3 == 1 ? 1 : 0) + (i % 8 == 3 ? 1 : 0) + (i % 7 == 4 ? 1 : 0);
            CHECK_EQUAL(expected_size, st->size());
            size_t ndx = 0;
            if (i % 3 == 1) {
                CHECK_EQUAL(700 + i, st->get_int(0, ndx));
                ++ndx;
            }
            if (i % 8 == 3) {
                CHECK_EQUAL(800 + i, st->get_int(0, ndx));
                ++ndx;
            }
            if (i % 7 == 4) {
                CHECK_EQUAL(900 + i, st->get_int(0, ndx));
                ++ndx;
            }
        }
    }
}


TEST(Group_MultiLevelSubtables)
{
    GROUP_TEST_PATH(path_1);
    GROUP_TEST_PATH(path_2);
    GROUP_TEST_PATH(path_3);
    GROUP_TEST_PATH(path_4);
    GROUP_TEST_PATH(path_5);

    {
        Group g;
        TableRef table = g.add_table("test");
        {
            DescriptorRef sub_1, sub_2;
            table->add_column(type_Int, "int");
            table->add_column(type_Table, "tab", &sub_1);
            table->add_column(type_Mixed, "mix");
            sub_1->add_column(type_Int, "int");
            sub_1->add_column(type_Table, "tab", &sub_2);
            sub_2->add_column(type_Int, "int");
        }
        table->add_empty_row();
        {
            TableRef a = table->get_subtable(1, 0);
            a->add_empty_row();
            TableRef b = a->get_subtable(1, 0);
            b->add_empty_row();
        }
        {
            table->set_mixed(2, 0, Mixed::subtable_tag());
            TableRef a = table->get_subtable(2, 0);
            a->add_column(type_Int, "int");
            a->add_column(type_Mixed, "mix");
            a->add_empty_row();
            a->set_mixed(1, 0, Mixed::subtable_tag());
            TableRef b = a->get_subtable(1, 0);
            b->add_column(type_Int, "int");
            b->add_empty_row();
        }
        g.write(path_1, crypt_key());
    }

    // Non-mixed
    {
        Group g(path_1, crypt_key());
        TableRef table = g.get_table("test");
        // Get A as subtable
        TableRef a = table->get_subtable(1, 0);
        // Get B as subtable from A
        TableRef b = a->get_subtable(1, 0);
        // Modify B
        b->set_int(0, 0, 6661012);
        // Modify A
        a->set_int(0, 0, 6661011);
        // Modify top
        table->set_int(0, 0, 6661010);
        // Get a second ref to A (compare)
        CHECK_EQUAL(table->get_subtable(1, 0), a);
        CHECK_EQUAL(table->get_subtable(1, 0)->get_int(0, 0), 6661011);
        // get a second ref to B (compare)
        CHECK_EQUAL(a->get_subtable(1, 0), b);
        CHECK_EQUAL(a->get_subtable(1, 0)->get_int(0, 0), 6661012);
        g.write(path_2, crypt_key());
    }
    {
        Group g(path_2, crypt_key());
        TableRef table = g.get_table("test");
        // Get A as subtable
        TableRef a = table->get_subtable(1, 0);
        // Get B as subtable from A
        TableRef b = a->get_subtable(1, 0);
        // Drop reference to A
        a = TableRef();
        // Modify B
        b->set_int(0, 0, 6661013);
        // Get a third ref to A (compare)
        a = table->get_subtable(1, 0);
        CHECK_EQUAL(table->get_subtable(1, 0)->get_int(0, 0), 6661011);
        // Get third ref to B and verify last mod
        b = a->get_subtable(1, 0);
        CHECK_EQUAL(a->get_subtable(1, 0)->get_int(0, 0), 6661013);
        g.write(path_3, crypt_key());
    }

    // Mixed
    {
        Group g(path_3, crypt_key());
        TableRef table = g.get_table("test");
        // Get A as subtable
        TableRef a = table->get_subtable(2, 0);
        // Get B as subtable from A
        TableRef b = a->get_subtable(1, 0);
        // Modify B
        b->set_int(0, 0, 6661012);
        // Modify A
        a->set_int(0, 0, 6661011);
        // Modify top
        table->set_int(0, 0, 6661010);
        // Get a second ref to A (compare)
        CHECK_EQUAL(table->get_subtable(2, 0), a);
        CHECK_EQUAL(table->get_subtable(2, 0)->get_int(0, 0), 6661011);
        // get a second ref to B (compare)
        CHECK_EQUAL(a->get_subtable(1, 0), b);
        CHECK_EQUAL(a->get_subtable(1, 0)->get_int(0, 0), 6661012);
        g.write(path_4, crypt_key());
    }
    {
        Group g(path_4, crypt_key());
        TableRef table = g.get_table("test");
        // Get A as subtable
        TableRef a = table->get_subtable(2, 0);
        // Get B as subtable from A
        TableRef b = a->get_subtable(1, 0);
        // Drop reference to A
        a = TableRef();
        // Modify B
        b->set_int(0, 0, 6661013);
        // Get a third ref to A (compare)
        a = table->get_subtable(2, 0);
        CHECK_EQUAL(table->get_subtable(2, 0)->get_int(0, 0), 6661011);
        // Get third ref to B and verify last mod
        b = a->get_subtable(1, 0);
        CHECK_EQUAL(a->get_subtable(1, 0)->get_int(0, 0), 6661013);
        g.write(path_5, crypt_key());
    }
}


TEST(Group_CommitSubtable)
{
    GROUP_TEST_PATH(path);
    Group group(path, crypt_key(), Group::mode_ReadWrite);

    TableRef table = group.add_table("test");
    DescriptorRef sub_1;
    table->add_column(type_Table, "subtable", &sub_1);
    sub_1->add_column(type_Int, "int");
    sub_1.reset();
    table->add_empty_row();

    TableRef subtable = table->get_subtable(0, 0);
    subtable->add_empty_row();

    group.commit();

    table->add_empty_row();
    group.commit();

    subtable = table->get_subtable(0, 0);
    subtable->add_empty_row();
    group.commit();

    table->add_empty_row();
    subtable = table->get_subtable(0, 0);
    subtable->add_empty_row();
    group.commit();
    group.verify();

    TableRef table1 = group.add_table("other");
    table1->add_column_link(type_LinkList, "linkList", *table);
    group.commit();
    group.verify();
    table->insert_column_link(0, type_Link, "link", *table);
    group.commit();
    group.verify();
}


TEST(Group_CommitSubtableMixed)
{
    GROUP_TEST_PATH(path);
    Group group(path, crypt_key(), Group::mode_ReadWrite);

    TableRef table = group.add_table("test");
    table->add_column(type_Mixed, "mixed");

    table->add_empty_row();

    table->clear_subtable(0, 0);
    TableRef subtable = table->get_subtable(0, 0);
    subtable->add_column(type_Int, "int");
    subtable->add_empty_row();

    group.commit();

    table->add_empty_row();
    group.commit();

    subtable = table->get_subtable(0, 0);
    subtable->add_empty_row();
    group.commit();

    table->add_empty_row();
    subtable = table->get_subtable(0, 0);
    subtable->add_empty_row();
    group.commit();
}


TEST(Group_CommitDegenerateSubtable)
{
    GROUP_TEST_PATH(path);
    Group group(path, crypt_key(), Group::mode_ReadWrite);
    TableRef table = group.add_table("parent");
    table->add_column(type_Table, "");
    table->get_subdescriptor(0)->add_column(type_Int, "");
    table->add_empty_row();
    TableRef subtab = table->get_subtable(0, 0);
    CHECK(subtab->is_degenerate());
    group.commit();
    CHECK(subtab->is_degenerate());
}

TEST(Group_InvalidateTables)
{
    TableRef table;
    TableRef subtable1;
    TableRef subtable2;
    TableRef subtable3;
    {
        Group group;
        table = group.add_table("foo");
        table->add_column(type_Mixed, "first");
        DescriptorRef descr1;
        DescriptorRef descr2;
        table->add_column(type_Table, "second", &descr1);
        test_table_add_columns(descr1);
        table->add_column(type_Table, "third", &descr2);
        test_table_add_columns(descr2);
        CHECK(table->is_attached());
        table->add_empty_row();
        table->set_mixed(0, 0, Mixed::subtable_tag());
        CHECK(table->is_attached());
        subtable1 = table->get_subtable(0, 0);
        CHECK(table->is_attached());
        CHECK(subtable1);
        CHECK(subtable1->is_attached());
        subtable2 = table->get_subtable(1, 0);
        CHECK(table->is_attached());
        CHECK(subtable1->is_attached());
        CHECK(subtable2);
        CHECK(subtable2->is_attached());
        subtable3 = table->get_subtable(2, 0);
        CHECK(table->is_attached());
        CHECK(subtable1->is_attached());
        CHECK(subtable2->is_attached());
        CHECK(subtable3);
        CHECK(subtable3->is_attached());
        add(subtable3, "alpha", 79542, true, Wed);
        add(subtable3, "beta", 97, false, Mon);
        CHECK(table->is_attached());
        CHECK(subtable1->is_attached());
        CHECK(subtable2->is_attached());
        CHECK(subtable3->is_attached());
    }
    CHECK(!table->is_attached());
    CHECK(!subtable1->is_attached());
    CHECK(!subtable2->is_attached());
    CHECK(!subtable3->is_attached());
}


TEST(Group_ToJSON)
{
    Group g;
    TableRef table = g.add_table("test");
    test_table_add_columns(table);

    add(table, "jeff", 1, true, Wed);
    add(table, "jim", 1, true, Wed);
    std::ostringstream out;
    g.to_json(out);
    std::string str = out.str();
    CHECK(str.length() > 0);
    CHECK_EQUAL("{\"test\":[{\"first\":\"jeff\",\"second\":1,\"third\":true,\"fourth\":2},{\"first\":\"jim\","
                "\"second\":1,\"third\":true,\"fourth\":2}]}",
                str);
}


TEST(Group_ToString)
{
    Group g;
    TableRef table = g.add_table("test");
    test_table_add_columns(table);

    add(table, "jeff", 1, true, Wed);
    add(table, "jim", 1, true, Wed);
    std::ostringstream out;
    g.to_string(out);
    std::string str = out.str();
    CHECK(str.length() > 0);
    CHECK_EQUAL("     tables     rows  \n   0 test       2     \n", str.c_str());
}


TEST(Group_IndexString)
{
    Group to_mem;
    TableRef table = to_mem.add_table("test");
    test_table_add_columns(table);

    add(table, "jeff", 1, true, Wed);
    add(table, "jim", 1, true, Wed);
    add(table, "jennifer", 1, true, Wed);
    add(table, "john", 1, true, Wed);
    add(table, "jimmy", 1, true, Wed);
    add(table, "jimbo", 1, true, Wed);
    add(table, "johnny", 1, true, Wed);
    add(table, "jennifer", 1, true, Wed); // duplicate

    table->add_search_index(0);
    CHECK(table->has_search_index(0));

    size_t r1 = table->find_first_string(0, "jimmi");
    CHECK_EQUAL(not_found, r1);

    size_t r2 = table->find_first_string(0, "jeff");
    size_t r3 = table->find_first_string(0, "jim");
    size_t r4 = table->find_first_string(0, "jimbo");
    size_t r5 = table->find_first_string(0, "johnny");
    CHECK_EQUAL(0, r2);
    CHECK_EQUAL(1, r3);
    CHECK_EQUAL(5, r4);
    CHECK_EQUAL(6, r5);

    size_t c1 = table->count_string(0, "jennifer");
    CHECK_EQUAL(2, c1);

    // Serialize to memory (we now own the buffer)
    BinaryData buffer = to_mem.write_to_mem();

    // Load the table
    Group from_mem(buffer);
    TableRef t = from_mem.get_table("test");
    CHECK_EQUAL(4, t->get_column_count());
    CHECK_EQUAL(8, t->size());

    CHECK(t->has_search_index(0));

    size_t m1 = t->find_first_string(0, "jimmi");
    CHECK_EQUAL(not_found, m1);

    size_t m2 = t->find_first_string(0, "jeff");
    size_t m3 = t->find_first_string(0, "jim");
    size_t m4 = t->find_first_string(0, "jimbo");
    size_t m5 = t->find_first_string(0, "johnny");
    CHECK_EQUAL(0, m2);
    CHECK_EQUAL(1, m3);
    CHECK_EQUAL(5, m4);
    CHECK_EQUAL(6, m5);

    size_t m6 = t->count_string(0, "jennifer");
    CHECK_EQUAL(2, m6);

    // Remove the search index and verify
    t->remove_search_index(0);
    CHECK(!t->has_search_index(0));
    from_mem.verify();

    size_t m7 = t->find_first_string(0, "jimmi");
    size_t m8 = t->find_first_string(0, "johnny");
    CHECK_EQUAL(not_found, m7);
    CHECK_EQUAL(6, m8);
}


TEST(Group_StockBug)
{
    // This test is a regression test - it once triggered a bug.
    // the bug was fixed in pr 351. In release mode, it crashes
    // the application. To get an assert in debug mode, the max
    // list size should be set to 1000.
    GROUP_TEST_PATH(path);
    Group group(path, crypt_key(), Group::mode_ReadWrite);

    TableRef table = group.add_table("stocks");
    table->add_column(type_String, "ticker");

    for (size_t i = 0; i < 100; ++i) {
        table->verify();
        table->insert_empty_row(i);
        table->set_string(0, i, "123456789012345678901234567890123456789");
        table->verify();
        group.commit();
    }
}


TEST(Group_CommitLinkListChange)
{
    GROUP_TEST_PATH(path);
    Group group(path, crypt_key(), Group::mode_ReadWrite);
    TableRef origin = group.add_table("origin");
    TableRef target = group.add_table("target");
    origin->add_column_link(type_LinkList, "", *target);
    target->add_column(type_Int, "");
    origin->add_empty_row();
    target->add_empty_row();
    LinkViewRef link_list = origin->get_linklist(0, 0);
    link_list->add(0);
    group.commit();
    group.verify();
}


TEST(Group_Commit_Update_Integer_Index)
{
    // This reproduces a bug where a commit would fail to update the Column::m_search_index pointer
    // and hence crash or behave erratic for subsequent index operations
    GROUP_TEST_PATH(path);

    Group g(path, 0, Group::mode_ReadWrite);
    TableRef t = g.add_table("table");
    t->add_column(type_Int, "integer");

    for (size_t i = 0; i < 200; i++) {
        t->add_empty_row();
        t->set_int(0, i, (i + 1) * 0xeeeeeeeeeeeeeeeeULL);
    }

    t->add_search_index(0);

    // This would always work
    CHECK(t->find_first_int(0, (0 + 1) * 0xeeeeeeeeeeeeeeeeULL) == 0);

    g.commit();

    // This would fail (sometimes return not_found, sometimes crash)
    CHECK(t->find_first_int(0, (0 + 1) * 0xeeeeeeeeeeeeeeeeULL) == 0);
}


TEST(Group_CascadeNotify_Simple)
{
    GROUP_TEST_PATH(path);

    Group g(path, 0, Group::mode_ReadWrite);
    TableRef t = g.add_table("target");
    t->add_column(type_Int, "int");

    // Add some extra rows so that the indexes being tested aren't all 0
    t->add_empty_row(100);

    bool called = false;
    g.set_cascade_notification_handler([&](const Group::CascadeNotification&) { called = true; });
    t->remove(5);
    CHECK(called);

    // move_last_over() on a table with no (back)links just sends that single
    // row in the notification
    called = false;
    g.set_cascade_notification_handler([&](const Group::CascadeNotification& notification) {
        called = true;
        CHECK_EQUAL(0, notification.links.size());
        CHECK_EQUAL(1, notification.rows.size());
        CHECK_EQUAL(0, notification.rows[0].table_ndx);
        CHECK_EQUAL(5, notification.rows[0].row_ndx);
    });
    t->move_last_over(5);
    CHECK(called);

    // Add another table which links to the target table
    TableRef origin = g.add_table("origin");
    origin->add_column_link(type_Link, "link", *t);
    origin->add_column_link(type_LinkList, "linklist", *t);

    origin->add_empty_row(100);

    // calling remove() is now an error, so no more tests of it

    // move_last_over() on an un-linked-to row should still just send that row
    // in the notification
    called = false;
    g.set_cascade_notification_handler([&](const Group::CascadeNotification& notification) {
        called = true;
        CHECK_EQUAL(0, notification.links.size());
        CHECK_EQUAL(1, notification.rows.size());
        CHECK_EQUAL(0, notification.rows[0].table_ndx);
        CHECK_EQUAL(5, notification.rows[0].row_ndx);
    });
    t->move_last_over(5);
    CHECK(called);

    // move_last_over() on a linked-to row should send information about the
    // links which had linked to it
    origin->set_link(0, 10, 11); // rows are arbitrarily different to make things less likely to pass by coincidence
    LinkViewRef lv = origin->get_linklist(1, 15);
    lv->add(11);
    lv->add(30);
    called = false;
    g.set_cascade_notification_handler([&](const Group::CascadeNotification& notification) {
        called = true;
        CHECK_EQUAL(1, notification.rows.size());
        CHECK_EQUAL(0, notification.rows[0].table_ndx);
        CHECK_EQUAL(11, notification.rows[0].row_ndx);

        CHECK_EQUAL(2, notification.links.size());
        CHECK_EQUAL(0, notification.links[0].origin_col_ndx);
        CHECK_EQUAL(10, notification.links[0].origin_row_ndx);
        CHECK_EQUAL(11, notification.links[0].old_target_row_ndx);

        CHECK_EQUAL(1, notification.links[1].origin_col_ndx);
        CHECK_EQUAL(15, notification.links[1].origin_row_ndx);
        CHECK_EQUAL(11, notification.links[1].old_target_row_ndx);
    });
    t->move_last_over(11);
    CHECK(called);

    // move_last_over() on the origin table just sends the row being removed
    // because the links are weak
    origin->set_link(0, 10, 11);
    origin->get_linklist(1, 10)->add(11);
    called = false;
    g.set_cascade_notification_handler([&](const Group::CascadeNotification& notification) {
        called = true;
        CHECK_EQUAL(1, notification.rows.size());
        CHECK_EQUAL(1, notification.rows[0].table_ndx);
        CHECK_EQUAL(10, notification.rows[0].row_ndx);

        CHECK_EQUAL(0, notification.links.size());
    });
    origin->move_last_over(10);
    CHECK(called);

    // move_last_over() on the origin table with strong links lists the target
    // rows that are removed
    origin->get_descriptor()->set_link_type(0, link_Strong);
    origin->get_descriptor()->set_link_type(1, link_Strong);

    origin->set_link(0, 10, 50);
    origin->set_link(0, 11, 62);
    lv = origin->get_linklist(1, 10);
    lv->add(60);
    lv->add(61);
    lv->add(61);
    lv->add(62);
    // 50, 60 and 61 should be removed; 62 should not as there's still a strong link
    called = false;
    g.set_cascade_notification_handler([&](const Group::CascadeNotification& notification) {
        called = true;
        CHECK_EQUAL(4, notification.rows.size());
        CHECK_EQUAL(0, notification.rows[0].table_ndx);
        CHECK_EQUAL(50, notification.rows[0].row_ndx);
        CHECK_EQUAL(0, notification.rows[1].table_ndx);
        CHECK_EQUAL(60, notification.rows[1].row_ndx);
        CHECK_EQUAL(0, notification.rows[2].table_ndx);
        CHECK_EQUAL(61, notification.rows[2].row_ndx);
        CHECK_EQUAL(1, notification.rows[3].table_ndx);
        CHECK_EQUAL(10, notification.rows[3].row_ndx);

        CHECK_EQUAL(0, notification.links.size());
    });
    origin->move_last_over(10);
    CHECK(called);

    g.set_cascade_notification_handler(nullptr);
    t->clear();
    origin->clear();
    t->add_empty_row(100);
    origin->add_empty_row(100);

    // Indirect nullifications: move_last_over() on a row with the last strong
    // links to a row that still has weak links to it
    origin->add_column_link(type_Link, "link2", *t);
    origin->add_column_link(type_LinkList, "linklist2", *t);

    CHECK_EQUAL(0, t->get_backlink_count(30, *origin, 0));
    CHECK_EQUAL(0, t->get_backlink_count(30, *origin, 1));
    CHECK_EQUAL(0, t->get_backlink_count(30, *origin, 2));
    CHECK_EQUAL(0, t->get_backlink_count(30, *origin, 3));
    origin->set_link(0, 20, 30);
    origin->get_linklist(1, 20)->add(31);
    origin->set_link(2, 25, 31);
    origin->get_linklist(3, 25)->add(30);
    CHECK_EQUAL(1, t->get_backlink_count(30, *origin, 0));
    CHECK_EQUAL(1, t->get_backlink_count(31, *origin, 1));
    CHECK_EQUAL(1, t->get_backlink_count(31, *origin, 2));
    CHECK_EQUAL(1, t->get_backlink_count(30, *origin, 3));

    called = false;
    g.set_cascade_notification_handler([&](const Group::CascadeNotification& notification) {
        called = true;
        CHECK_EQUAL(3, notification.rows.size());
        CHECK_EQUAL(0, notification.rows[0].table_ndx);
        CHECK_EQUAL(30, notification.rows[0].row_ndx);
        CHECK_EQUAL(0, notification.rows[1].table_ndx);
        CHECK_EQUAL(31, notification.rows[1].row_ndx);
        CHECK_EQUAL(1, notification.rows[2].table_ndx);
        CHECK_EQUAL(20, notification.rows[2].row_ndx);

        CHECK_EQUAL(2, notification.links.size());
        CHECK_EQUAL(3, notification.links[0].origin_col_ndx);
        CHECK_EQUAL(25, notification.links[0].origin_row_ndx);
        CHECK_EQUAL(30, notification.links[0].old_target_row_ndx);

        CHECK_EQUAL(2, notification.links[1].origin_col_ndx);
        CHECK_EQUAL(25, notification.links[1].origin_row_ndx);
        CHECK_EQUAL(31, notification.links[1].old_target_row_ndx);
    });
    origin->move_last_over(20);
    CHECK(called);
}


TEST(Group_CascadeNotify_TableClear)
{
    GROUP_TEST_PATH(path);

    Group g(path, 0, Group::mode_ReadWrite);
    TableRef t = g.add_table("target");
    t->add_column(type_Int, "int");

    t->add_empty_row(10);

    // clear() does not list the rows in the table being cleared because it
    // would be expensive and mostly pointless to do so
    bool called = false;
    g.set_cascade_notification_handler([&](const Group::CascadeNotification& notification) {
        called = true;
        CHECK_EQUAL(0, notification.links.size());
        CHECK_EQUAL(0, notification.rows.size());
    });
    t->clear();
    CHECK(called);

    // Add another table which links to the target table
    TableRef origin = g.add_table("origin");
    origin->add_column_link(type_Link, "link", *t);
    origin->add_column_link(type_LinkList, "linklist", *t);

    t->add_empty_row(10);
    origin->add_empty_row(10);

    // clear() does report nullified links
    origin->set_link(0, 1, 2);
    origin->get_linklist(1, 3)->add(4);
    called = false;
    g.set_cascade_notification_handler([&](const Group::CascadeNotification& notification) {
        called = true;
        CHECK_EQUAL(0, notification.rows.size());

        CHECK_EQUAL(2, notification.links.size());
        CHECK_EQUAL(0, notification.links[0].origin_col_ndx);
        CHECK_EQUAL(1, notification.links[0].origin_row_ndx);
        CHECK_EQUAL(2, notification.links[0].old_target_row_ndx);

        CHECK_EQUAL(1, notification.links[1].origin_col_ndx);
        CHECK_EQUAL(3, notification.links[1].origin_row_ndx);
        CHECK_EQUAL(4, notification.links[1].old_target_row_ndx);
    });
    t->clear();
    CHECK(called);

    t->add_empty_row(10);
    origin->add_empty_row(10);

    // and cascaded deletions
    origin->get_descriptor()->set_link_type(0, link_Strong);
    origin->get_descriptor()->set_link_type(1, link_Strong);
    origin->set_link(0, 1, 2);
    origin->get_linklist(1, 3)->add(4);
    called = false;
    g.set_cascade_notification_handler([&](const Group::CascadeNotification& notification) {
        called = true;
        CHECK_EQUAL(0, notification.links.size());
        CHECK_EQUAL(2, notification.rows.size());
        CHECK_EQUAL(0, notification.rows[0].table_ndx);
        CHECK_EQUAL(2, notification.rows[0].row_ndx);
        CHECK_EQUAL(0, notification.rows[1].table_ndx);
        CHECK_EQUAL(4, notification.rows[1].row_ndx);
    });
    origin->clear();
    CHECK(called);
}


TEST(Group_CascadeNotify_TableViewClear)
{
    GROUP_TEST_PATH(path);

    Group g(path, 0, Group::mode_ReadWrite);
    TableRef t = g.add_table("target");
    t->add_column(type_Int, "int");

    t->add_empty_row(10);

    // No link columns, so remove() is used
    // Unlike clearing a table, the rows removed by the clear() are included in
    // the notification so that cascaded deletions and direct deletions don't
    // need to be handled separately
    bool called = false;
    g.set_cascade_notification_handler([&](const Group::CascadeNotification& notification) {
        called = true;
        CHECK_EQUAL(0, notification.links.size());
        CHECK_EQUAL(10, notification.rows.size());
    });
    t->where().find_all().clear();
    CHECK(called);

    // Add another table which links to the target table
    TableRef origin = g.add_table("origin");
    origin->add_column_link(type_Link, "link", *t);
    origin->add_column_link(type_LinkList, "linklist", *t);

    // Now has backlinks, so move_last_over() is used
    t->add_empty_row(10);
    called = false;
    g.set_cascade_notification_handler([&](const Group::CascadeNotification& notification) {
        called = true;
        CHECK_EQUAL(0, notification.links.size());
        CHECK_EQUAL(10, notification.rows.size());
    });
    t->where().find_all().clear();
    CHECK(called);

    t->add_empty_row(10);
    origin->add_empty_row(10);

    // should list which links were nullified
    origin->set_link(0, 1, 2);
    origin->get_linklist(1, 3)->add(4);
    called = false;
    g.set_cascade_notification_handler([&](const Group::CascadeNotification& notification) {
        called = true;
        CHECK_EQUAL(10, notification.rows.size());
        CHECK_EQUAL(2, notification.links.size());

        CHECK_EQUAL(0, notification.links[0].origin_col_ndx);
        CHECK_EQUAL(1, notification.links[0].origin_row_ndx);
        CHECK_EQUAL(2, notification.links[0].old_target_row_ndx);

        CHECK_EQUAL(1, notification.links[1].origin_col_ndx);
        CHECK_EQUAL(3, notification.links[1].origin_row_ndx);
        CHECK_EQUAL(4, notification.links[1].old_target_row_ndx);
    });
    t->where().find_all().clear();
    CHECK(called);

    g.set_cascade_notification_handler(nullptr);
    origin->clear();
    t->add_empty_row(10);
    origin->add_empty_row(10);

    // should included cascaded deletions
    origin->get_descriptor()->set_link_type(0, link_Strong);
    origin->get_descriptor()->set_link_type(1, link_Strong);
    origin->set_link(0, 1, 2);
    origin->get_linklist(1, 3)->add(4);
    called = false;
    g.set_cascade_notification_handler([&](const Group::CascadeNotification& notification) {
        called = true;
        CHECK_EQUAL(0, notification.links.size());
        CHECK_EQUAL(12, notification.rows.size()); // 10 from origin, 2 from target
        CHECK_EQUAL(0, notification.rows[0].table_ndx);
        CHECK_EQUAL(2, notification.rows[0].row_ndx);
        CHECK_EQUAL(0, notification.rows[1].table_ndx);
        CHECK_EQUAL(4, notification.rows[1].row_ndx);
    });
    origin->where().find_all().clear();
    CHECK(called);
}


TEST(Group_AddEmptyRowCrash)
{
    // Exposes former bug in ColumnBase::build().

    size_t a = REALM_MAX_BPNODE_SIZE * REALM_MAX_BPNODE_SIZE;
    size_t b = REALM_MAX_BPNODE_SIZE;

    Group group;
    TableRef table = group.add_table("table");
    table->add_column(type_Int, "i1");
    table->add_empty_row(a);

    table->add_empty_row(1); // Introduces 3rd level of B+-tree

    table->add_column(type_Int, "i2"); // Calls ColumnBase::create() with size = a+1

    table->add_empty_row(b - 1);

    // array.cpp:2008: [realm-core-0.95.4] Assertion failed: insert_ndx - 1 == REALM_MAX_BPNODE_SIZE [b+1, b]
    table->add_empty_row(1);
}


TEST_IF(Group_AddEmptyRowCrash_2, REALM_MAX_BPNODE_SIZE == 4)
{
    // Set REALM_MAX_BPNODE_SIZE = 4 for it to crash
    Group group;
    TableRef table = group.add_table("table");
    table->add_column(type_Int, "A");
    table->add_empty_row(147);
    table->add_column(type_Int, "B");
    table->add_empty_row(110);

    // column.hpp:1267: [realm-core-0.95.5] Assertion failed: prior_num_rows == size()
    table->add_empty_row();
}


TEST_IF(Group_AddEmptyRowCrash_3, REALM_MAX_BPNODE_SIZE == 4)
{
    // Set REALM_MAX_BPNODE_SIZE = 4 for it to crash
    Group g;
    g.insert_table(0, "A");
    g.add_table("B");
    g.get_table(0)->add_column_link(type_LinkList, "link", *g.get_table(1));
    g.get_table(1)->insert_empty_row(0, 17);
    g.get_table(1)->insert_empty_row(17, 1);

    // Triggers "alloc.hpp:213: Assertion failed: v % 8 == 0"
    g.verify();
}


TEST(Group_WriteEmpty)
{
    GROUP_TEST_PATH(path_1);
    GROUP_TEST_PATH(path_2);
    {
        Group group;
        group.write(path_2);
    }
    File::remove(path_2);
    {
        Group group(path_1, 0, Group::mode_ReadWrite);
        group.write(path_2);
    }
}


#ifdef REALM_DEBUG
#ifdef REALM_TO_DOT

TEST(Group_ToDot)
{
    // Create group with one table
    Group mygroup;

    // Create table with all column types
    TableRef table = mygroup.add_table("test");
    DescriptorRef subdesc;
    table->add_column(type_Int, "int");
    table->add_column(type_Bool, "bool");
    table->add_column(type_OldDateTime, "date");
    table->add_column(type_String, "string");
    table->add_column(type_String, "string_long");
    table->add_column(type_String, "string_enum"); // becomes StringEnumColumn
    table->add_column(type_Binary, "binary");
    table->add_column(type_Mixed, "mixed");
    table->add_column(type_Table, "tables", &subdesc);
    subdesc->add_column(type_Int, "sub_first");
    subdesc->add_column(type_String, "sub_second");
    subdesc.reset();

    // Add some rows
    for (size_t i = 0; i < 15; ++i) {
        table->insert_empty_row(i);
        table->set_int(0, i, i);
        table->set_bool(1, i, (i % 2 ? true : false));
        table->set_olddatetime(2, i, 12345);

        std::stringstream ss;
        ss << "string" << i;
        table->set_string(3, i, ss.str().c_str());

        ss << " very long string.........";
        table->set_string(4, i, ss.str().c_str());

        switch (i % 3) {
            case 0:
                table->set_string(5, i, "test1");
                break;
            case 1:
                table->set_string(5, i, "test2");
                break;
            case 2:
                table->set_string(5, i, "test3");
                break;
        }

        table->set_binary(6, i, BinaryData("binary", 7));

        switch (i % 3) {
            case 0:
                table->set_mixed(7, i, false);
                break;
            case 1:
                table->set_mixed(7, i, (int64_t)i);
                break;
            case 2:
                table->set_mixed(7, i, "string");
                break;
        }

        // Add sub-tables
        if (i == 2) {
            // To mixed column
            table->set_mixed(7, i, Mixed::subtable_tag());
            TableRef st = table->get_subtable(7, i);

            st->add_column(type_Int, "first");
            st->add_column(type_String, "second");

            st->insert_empty_row(0);
            st->set_int(0, 0, 42);
            st->set_string(1, 0, "meaning");

            // To table column
            TableRef subtable2 = table->get_subtable(8, i);
            subtable2->add_empty_row();
            subtable2->set_int(0, 0, 42);
            subtable2->set_string(1, 0, "meaning");
        }
    }

    // We also want StringEnumColumn's
    table->optimize();

#if 1
    // Write array graph to std::cout
    std::stringstream ss;
    mygroup.to_dot(ss);
    std::cout << ss.str() << std::endl;
#endif

    // Write array graph to file in dot format
    std::ofstream fs("realm_graph.dot", std::ios::out | std::ios::binary);
    if (!fs.is_open())
        std::cout << "file open error " << strerror(errno) << std::endl;
    mygroup.to_dot(fs);
    fs.close();
}

#endif // REALM_TO_DOT
#endif // REALM_DEBUG

TEST_TYPES(Group_TimestampAddAIndexAndThenInsertEmptyRows, std::true_type, std::false_type)
{
    constexpr bool nullable = TEST_TYPE::value;
    Group g;
    TableRef table = g.add_table("");
    table->insert_column(0, type_Timestamp, "", nullable);
    table->add_search_index(0);
    table->add_empty_row(5);
    CHECK_EQUAL(table->size(), 5);
}

TEST(Group_SharedMappingsForReadOnlyStreamingForm)
{
    GROUP_TEST_PATH(path);
    {
        Group g;
        auto table = g.add_table("table");
        table->add_column(type_Int, "col");
        table->add_empty_row();
        g.write(path, crypt_key());
    }

    {
        Group g1(path, crypt_key(), Group::mode_ReadOnly);
        auto table1 = g1.get_table("table");
        CHECK(table1 && table1->size() == 1);

        Group g2(path, crypt_key(), Group::mode_ReadOnly);
        auto table2 = g2.get_table("table");
        CHECK(table2 && table2->size() == 1);
    }
}


// This test embodies a current limitation of our merge algorithm. If this
// limitation is lifted, the code for the SET_UNIQUE instruction in
// fuzz_group.cpp should be strengthened to reflect this.
// (i.e. remove the try / catch for LogicError of kind illegal_combination)
TEST(Group_SetNullUniqueLimitation)
{
    Group g;
    TableRef t = g.add_table("t0");
    t->add_column(type_Int, "", true);
    t->add_search_index(0);
    t->add_column_link(type_LinkList, "", *t);
    t->add_empty_row();
    t->get_linklist(1, 0)->add(0);
    try {
        t->set_null_unique(0, 0);
    }
    catch (const LogicError& le) {
        CHECK(le.kind() == LogicError::illegal_combination);
    }
}

TEST(Group_RemoveRecursive)
{
    Group g;
    TableRef target = g.add_table("target");
    TableRef origin = g.add_table("origin");

    target->add_column(type_Int, "integers", true);
    target->add_column_link(type_Link, "links", *target);
    origin->add_column_link(type_Link, "links", *target);

    // Delete one at a time
    target->add_empty_row();
    origin->add_empty_row(2);
    origin->set_link(0, 0, 0);
    origin->set_link(0, 1, 0);
    CHECK_EQUAL(target->size(), 1);
    origin->remove_recursive(0);
    // Should not have deleted child
    CHECK_EQUAL(target->size(), 1);
    // Delete last link
    origin->remove_recursive(0);
    // Now it should be gone
    CHECK_EQUAL(target->size(), 0);

    // 3 rows linked together
    target->add_empty_row(3);
    target->set_link(1, 0, 1);
    target->set_link(1, 1, 2);
    bool called = false;
    g.set_cascade_notification_handler([&](const Group::CascadeNotification& notification) {
        called = true;
        CHECK_EQUAL(3, notification.rows.size());
        CHECK_EQUAL(0, notification.rows[0].table_ndx);
        CHECK_EQUAL(0, notification.rows[0].row_ndx);
        CHECK_EQUAL(0, notification.rows[1].table_ndx);
        CHECK_EQUAL(1, notification.rows[1].row_ndx);
        CHECK_EQUAL(0, notification.rows[2].table_ndx);
        CHECK_EQUAL(2, notification.rows[2].row_ndx);

        CHECK_EQUAL(0, notification.links.size());
    });
    target->remove_recursive(0);
    CHECK_EQUAL(target->size(), 0);

    // 3 rows linked together in circle
    target->add_empty_row(3);
    target->set_link(1, 0, 1);
    target->set_link(1, 1, 2);
    target->set_link(1, 2, 0);
    called = false;
    g.set_cascade_notification_handler([&](const Group::CascadeNotification& notification) {
        called = true;
        CHECK_EQUAL(3, notification.rows.size());
        CHECK_EQUAL(0, notification.rows[0].table_ndx);
        CHECK_EQUAL(0, notification.rows[0].row_ndx);
        CHECK_EQUAL(0, notification.rows[1].table_ndx);
        CHECK_EQUAL(1, notification.rows[1].row_ndx);
        CHECK_EQUAL(0, notification.rows[2].table_ndx);
        CHECK_EQUAL(2, notification.rows[2].row_ndx);

        CHECK_EQUAL(0, notification.links.size());
    });
    target->remove_recursive(0);
    CHECK_EQUAL(target->size(), 0);
}

#endif // TEST_GROUP
