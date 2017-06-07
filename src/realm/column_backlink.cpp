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

#include <algorithm>
#include <set> // FIXME: Used for swap

#include <realm/column_backlink.hpp>
#include <realm/column_link.hpp>
#include <realm/group.hpp>
#include <realm/table.hpp>

#include <realm/util/miscellaneous.hpp>

using namespace realm;

// void BacklinkColumn::add_backlink(size_t row_ndx, size_t origin_row_ndx)
void BacklinkColumn::add_backlink(Key target_key, Key origin_key)
{
    size_t row_ndx = m_table->get_row_ndx(target_key);
    uint64_t value = IntegerColumn::get_uint(row_ndx);

    // A backlink list of size 1 is stored as a single non-ref column value.
    if (value == 0) {
        IntegerColumn::set_uint(row_ndx, uint64_t(origin_key.value) << 1 | 1); // Throws
        return;
    }

    ref_type ref;
    // When increasing the size of the backlink list from 1 to 2, we need to
    // convert from the single non-ref column value representation, to a B+-tree
    // representation.
    if ((value & 1) != 0) {
        // Create new column to hold backlinks
        size_t init_size = 1;
        int_fast64_t value_2 = value >> 1;
        ref = IntegerColumn::create(get_alloc(), Array::type_Normal, init_size, value_2); // Throws
        IntegerColumn::set_as_ref(row_ndx, ref);                                          // Throws
    }
    else {
        ref = to_ref(value);
    }
    IntegerColumn backlink_list(get_alloc(), ref); // Throws
    backlink_list.set_parent(this, row_ndx);
    backlink_list.add(origin_key.value); // Throws
}


size_t BacklinkColumn::get_backlink_count(size_t row_ndx) const noexcept
{
    uint64_t value = IntegerColumn::get_uint(row_ndx);

    if (value == 0)
        return 0;

    if ((value & 1) != 0)
        return 1;

    // get list size
    ref_type ref = to_ref(value);
    return ColumnBase::get_size_from_ref(ref, get_alloc());
}


Key BacklinkColumn::get_backlink(size_t row_ndx, size_t backlink_ndx) const noexcept
{
    int64_t value = IntegerColumn::get(row_ndx);
    REALM_ASSERT_3(value, !=, 0);

    Key origin_key;
    if ((value & 1) != 0) {
        REALM_ASSERT_3(backlink_ndx, ==, 0);
        origin_key = Key(value >> 1);
    }
    else {
        ref_type ref = to_ref(value);
        REALM_ASSERT_3(backlink_ndx, <, ColumnBase::get_size_from_ref(ref, get_alloc()));
        // FIXME: Optimize with direct access (that is, avoid creation of a
        // Column instance, since that implies dynamic allocation).
        IntegerColumn backlink_list(get_alloc(), ref); // Throws
        int64_t value_2 = backlink_list.get(backlink_ndx);
        origin_key = Key(value_2);
    }
    return origin_key;
}


// void BacklinkColumn::remove_one_backlink(size_t row_ndx, size_t origin_row_ndx)
void BacklinkColumn::remove_one_backlink(Key target_key, Key origin_key)
{
    size_t row_ndx = m_table->get_row_ndx(target_key);
    uint64_t value = IntegerColumn::get_uint(row_ndx);
    REALM_ASSERT_3(value, !=, 0);

    // If there is only a single backlink, it can be stored as
    // a tagged value
    if ((value & 1) != 0) {
        REALM_ASSERT_3(value >> 1, ==, uint64_t(origin_key.value));
        IntegerColumn::set(row_ndx, 0);
        return;
    }

    // if there is a list of backlinks we have to find
    // the right one and remove it.
    ref_type ref = to_ref(value);
    IntegerColumn backlink_list(get_alloc(), ref); // Throws
    backlink_list.set_parent(this, row_ndx);
    size_t backlink_ndx = backlink_list.find_first(origin_key.value);
    REALM_ASSERT_3(backlink_ndx, !=, not_found);
    backlink_list.erase(backlink_ndx); // Throws

    // If there is only one backlink left we can inline it as tagged value
    if (backlink_list.size() == 1) {
        uint64_t value_3 = backlink_list.get_uint(0);
        backlink_list.destroy();

        int_fast64_t value_4 = value_3 << 1 | 1;
        IntegerColumn::set_uint(row_ndx, value_4);
    }
}


void BacklinkColumn::remove_all_backlinks(size_t num_rows)
{
    Allocator& alloc = get_alloc();
    for (size_t row_ndx = 0; row_ndx < num_rows; ++row_ndx) {
        // List lists with more than one element are represented by a B+ tree,
        // whose nodes need to be freed.
        uint64_t value = IntegerColumn::get(row_ndx);
        if (value && (value & 1) == 0) {
            ref_type ref = to_ref(value);
            Array::destroy_deep(ref, alloc);
        }
        IntegerColumn::set(row_ndx, 0);
    }
}


void BacklinkColumn::update_backlink(size_t row_ndx, size_t old_origin_row_ndx, size_t new_origin_row_ndx)
{
    uint64_t value = IntegerColumn::get_uint(row_ndx);
    REALM_ASSERT_3(value, !=, 0);

    if ((value & 1) != 0) {
        REALM_ASSERT_3(to_size_t(value >> 1), ==, old_origin_row_ndx);
        uint64_t value_2 = new_origin_row_ndx << 1 | 1;
        IntegerColumn::set_uint(row_ndx, value_2);
        return;
    }

    // Find match in backlink list and replace
    ref_type ref = to_ref(value);
    IntegerColumn backlink_list(get_alloc(), ref); // Throws
    backlink_list.set_parent(this, row_ndx);
    int_fast64_t value_2 = int_fast64_t(old_origin_row_ndx);
    size_t backlink_ndx = backlink_list.find_first(value_2);
    REALM_ASSERT_3(backlink_ndx, !=, not_found);
    int_fast64_t value_3 = int_fast64_t(new_origin_row_ndx);
    backlink_list.set(backlink_ndx, value_3);
}

void BacklinkColumn::swap_backlinks(size_t row_ndx, size_t origin_row_ndx_1, size_t origin_row_ndx_2)
{
    uint64_t value = Column::get_uint(row_ndx);
    REALM_ASSERT_3(value, !=, 0);

    if ((value & 1) != 0) {
        uint64_t r = value >> 1;
        if (r == origin_row_ndx_1) {
            IntegerColumn::set_uint(row_ndx, origin_row_ndx_2 << 1 | 1);
        }
        else if (r == origin_row_ndx_2) {
            IntegerColumn::set_uint(row_ndx, origin_row_ndx_1 << 1 | 1);
        }
        return;
    }

    // Find matches in backlink list and replace
    ref_type ref = to_ref(value);
    IntegerColumn backlink_list(get_alloc(), ref); // Throws
    backlink_list.set_parent(this, row_ndx);
    size_t num_backlinks = backlink_list.size();
    for (size_t i = 0; i < num_backlinks; ++i) {
        uint64_t r = backlink_list.get_uint(i);
        if (r == origin_row_ndx_1) {
            backlink_list.set(i, origin_row_ndx_2);
        }
        else if (r == origin_row_ndx_2) {
            backlink_list.set(i, origin_row_ndx_1);
        }
    }
}


template <typename Func>
size_t BacklinkColumn::for_each_link(size_t row_ndx, bool do_destroy, Func&& func)
{
    int_fast64_t value = IntegerColumn::get(row_ndx);
    if (value != 0) {
        if ((value & 1) != 0) {
            Key origin_key = Key(value >> 1);
            func(origin_key); // Throws
        }
        else {
            ref_type ref = to_ref(value);
            IntegerColumn backlink_list(get_alloc(), ref); // Throws

            size_t n = backlink_list.size();
            for (size_t i = 0; i < n; ++i) {
                int_fast64_t value_2 = backlink_list.get(i);
                Key origin_key = Key(value_2);
                func(origin_key); // Throws
            }

            if (do_destroy)
                backlink_list.destroy();
        }
    }
    return to_size_t(value);
}


void BacklinkColumn::insert_rows(size_t row_ndx, size_t num_rows_to_insert, size_t prior_num_rows, bool insert_nulls)
{
    REALM_ASSERT_DEBUG(prior_num_rows == size());
    REALM_ASSERT(row_ndx <= prior_num_rows);
    REALM_ASSERT(!insert_nulls);

    IntegerColumn::insert_rows(row_ndx, num_rows_to_insert, prior_num_rows, insert_nulls); // Throws
}


void BacklinkColumn::erase_rows(size_t row_ndx, size_t num_rows_to_erase, size_t prior_num_rows,
                                bool broken_reciprocal_backlinks)
{
    REALM_ASSERT_DEBUG(prior_num_rows == size());
    REALM_ASSERT(num_rows_to_erase <= prior_num_rows);
    REALM_ASSERT(row_ndx <= prior_num_rows - num_rows_to_erase);

    // Nullify forward links to the removed target rows
    for (size_t i = 0; i < num_rows_to_erase; ++i) {
        Key target_key = m_table->get_key(row_ndx + i);
        auto handler = [=](Key origin_key) {
            m_origin_column->do_nullify_link(origin_key, target_key); // Throws
        };
        bool do_destroy = true;
        for_each_link(row_ndx + i, do_destroy, handler); // Throws
    }

    IntegerColumn::erase_rows(row_ndx, num_rows_to_erase, prior_num_rows, broken_reciprocal_backlinks); // Throws
}


void BacklinkColumn::move_last_row_over(size_t row_ndx, size_t prior_num_rows, bool broken_reciprocal_backlinks)
{
    REALM_ASSERT_DEBUG(prior_num_rows == size());
    REALM_ASSERT(row_ndx < prior_num_rows);

    // Nullify forward links to the removed target row
    Key target_key = m_table->get_key(row_ndx);
    auto handler = [=](Key origin_key) {
        m_origin_column->do_nullify_link(origin_key, target_key); // Throws
    };
    bool do_destroy = true;
    for_each_link(row_ndx, do_destroy, handler); // Throws

    IntegerColumn::move_last_row_over(row_ndx, prior_num_rows, broken_reciprocal_backlinks); // Throws
}


void BacklinkColumn::swap_rows(size_t row_ndx_1, size_t row_ndx_2)
{
    //    std::set<Key> unique_origin_keys;
    //    const bool do_destroy = false;
    //    for_each_link(row_ndx_1, do_destroy, [&](Key origin_Key) { unique_origin_keys.insert(origin_Key); });
    //    for_each_link(row_ndx_2, do_destroy, [&](Key origin_Key) { unique_origin_keys.insert(origin_Key); });
    //
    //    for (const auto& origin_row : unique_origin_keys) {
    //        m_origin_column->do_swap_link(origin_row, row_ndx_1, row_ndx_2);
    //    }

    IntegerColumn::swap_rows(row_ndx_1, row_ndx_2);
}


void BacklinkColumn::clear(size_t num_rows, bool)
{
    for (size_t row_ndx = 0; row_ndx < num_rows; ++row_ndx) {
        // IntegerColumn::clear() handles the destruction of subtrees
        bool do_destroy = false;
        Key target_key = m_table->get_key(row_ndx);
        for_each_link(row_ndx, do_destroy, [=](Key origin_key) {
            m_origin_column->do_nullify_link(origin_key, target_key); // Throws
        });
    }

    clear_without_updating_index(); // Throws
    // FIXME: This one is needed because
    // IntegerColumn::clear_without_updating_index() forgets about the leaf
    // type. A better solution should probably be found.
    get_root_array()->set_type(Array::type_HasRefs);
}


void BacklinkColumn::update_child_ref(size_t child_ndx, ref_type new_ref)
{
    IntegerColumn::set(child_ndx, new_ref); // Throws
}


ref_type BacklinkColumn::get_child_ref(size_t child_ndx) const noexcept
{
    return IntegerColumn::get_as_ref(child_ndx);
}

void BacklinkColumn::cascade_break_backlinks_to(size_t row_ndx, CascadeState& state)
{
    if (state.track_link_nullifications) {
        Key old_target_key = m_table->get_key(row_ndx);
        bool do_destroy = false;
        for_each_link(row_ndx, do_destroy, [&](Key origin_key) {
            state.links.push_back({m_origin_table.get(), get_origin_column_index(), origin_key, old_target_key});
        });
    }
}

void BacklinkColumn::cascade_break_backlinks_to_all_rows(size_t num_rows, CascadeState& state)
{
    if (state.track_link_nullifications) {
        for (size_t row_ndx = 0; row_ndx < num_rows; ++row_ndx) {
            Key old_target_key = m_table->get_key(row_ndx);
            // IntegerColumn::clear() handles the destruction of subtrees
            bool do_destroy = false;
            for_each_link(row_ndx, do_destroy, [&](Key origin_key) {
                state.links.push_back({m_origin_table.get(), get_origin_column_index(), origin_key, old_target_key});
            });
        }
    }
}

int BacklinkColumn::compare_values(size_t, size_t) const noexcept
{
    REALM_ASSERT(false); // backlinks can only be queried over and not on directly
    return 0;
}

// LCOV_EXCL_START ignore debug functions

#ifdef REALM_DEBUG

namespace {

size_t verify_leaf(MemRef mem, Allocator& alloc)
{
    Array leaf(alloc);
    leaf.init_from_mem(mem);
    leaf.verify();
    REALM_ASSERT(leaf.has_refs());
    return leaf.size();
}

} // anonymous namespace

#endif

void BacklinkColumn::verify() const
{
#ifdef REALM_DEBUG
    if (root_is_leaf()) {
        get_root_array()->verify();
        REALM_ASSERT(get_root_array()->has_refs());
        return;
    }

    get_root_array()->verify_bptree(&verify_leaf);
#endif
}

void BacklinkColumn::verify(const Table& table, size_t col_ndx) const
{
#ifdef REALM_DEBUG
    IntegerColumn::verify(table, col_ndx);

    // Check that the origin column specifies the right target
    REALM_ASSERT(&m_origin_column->get_target_table() == &table);
    REALM_ASSERT(&m_origin_column->get_backlink_column() == this);

    // Check that m_origin_table is the table specified by the spec
    size_t origin_table_ndx = m_origin_table->get_index_in_group();
    typedef _impl::TableFriend tf;
    const Spec& spec = tf::get_spec(table);
    REALM_ASSERT_3(origin_table_ndx, ==, spec.get_opposite_link_table_ndx(col_ndx));
#else
    static_cast<void>(table);
    static_cast<void>(col_ndx);
#endif
}

#ifdef REALM_DEBUG
void BacklinkColumn::get_backlinks(std::vector<VerifyPair>& pairs)
{
    VerifyPair pair;
    size_t n = size();
    for (size_t i = 0; i < n; ++i) {
        pair.target_key = m_table->get_key(i);
        size_t m = get_backlink_count(i);
        for (size_t j = 0; j < m; ++j) {
            pair.origin_key = get_backlink(i, j);
            pairs.push_back(pair);
        }
    }
    sort(pairs.begin(), pairs.end());
}
#endif

std::pair<ref_type, size_t> BacklinkColumn::get_to_dot_parent(size_t ndx_in_parent) const
{
    return IntegerColumn::get_to_dot_parent(ndx_in_parent);
}

// LCOV_EXCL_STOP ignore debug functions
