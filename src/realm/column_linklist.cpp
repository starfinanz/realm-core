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
#include <set>

#include <realm/column_linklist.hpp>
#include <realm/group.hpp>
#include <realm/link_view.hpp>

using namespace realm;


void LinkListColumn::insert_rows(size_t row_ndx, size_t num_rows_to_insert, size_t prior_num_rows, bool insert_nulls)
{
    REALM_ASSERT_DEBUG(prior_num_rows == size());
    REALM_ASSERT(row_ndx <= prior_num_rows);
    REALM_ASSERT(!insert_nulls);

    // Update backlinks to the moved origin rows
    size_t num_rows_moved = prior_num_rows - row_ndx;
    for (size_t i = num_rows_moved; i > 0; --i) {
        size_t old_origin_row_ndx = row_ndx + i - 1;
        size_t new_origin_row_ndx = row_ndx + num_rows_to_insert + i - 1;
        if (ref_type ref = get_as_ref(old_origin_row_ndx)) {
            IntegerColumn link_list(get_alloc(), ref);
            size_t n = link_list.size();
            for (size_t j = 0; j < n; ++j) {
                uint_fast64_t value = link_list.get_uint(j);
                size_t target_row_ndx = to_size_t(value);
                m_backlink_column->update_backlink(target_row_ndx, old_origin_row_ndx, new_origin_row_ndx); // Throws
            }
        }
    }

    LinkColumnBase::insert_rows(row_ndx, num_rows_to_insert, prior_num_rows, insert_nulls); // Throws

    if (num_rows_moved > 0) {
        const bool fix_ndx_in_parent = true;
        adj_insert_rows<fix_ndx_in_parent>(row_ndx, num_rows_to_insert);
    }
}


void LinkListColumn::erase_rows(size_t row_ndx, size_t num_rows_to_erase, size_t prior_num_rows,
                                bool broken_reciprocal_backlinks)
{
    REALM_ASSERT_DEBUG(prior_num_rows == size());
    REALM_ASSERT(num_rows_to_erase <= prior_num_rows);
    REALM_ASSERT(row_ndx <= prior_num_rows - num_rows_to_erase);

    // Remove backlinks to the removed origin rows
    for (size_t i = 0; i < num_rows_to_erase; ++i) {
        if (ref_type ref = get_as_ref(row_ndx + i)) {
            if (!broken_reciprocal_backlinks) {
                IntegerColumn link_list(get_alloc(), ref);
                size_t n = link_list.size();
                for (size_t j = 0; j < n; ++j) {
                    size_t target_row_ndx = to_size_t(link_list.get(j));
                    m_backlink_column->remove_one_backlink(target_row_ndx, row_ndx + i);
                }
            }
            Array::destroy_deep(ref, get_alloc());
        }
    }

    // Update backlinks to the moved origin rows
    size_t num_rows_moved = prior_num_rows - (row_ndx + num_rows_to_erase);
    for (size_t i = 0; i < num_rows_moved; ++i) {
        size_t old_origin_row_ndx = row_ndx + num_rows_to_erase + i;
        size_t new_origin_row_ndx = row_ndx + i;
        if (ref_type ref = get_as_ref(old_origin_row_ndx)) {
            IntegerColumn link_list(get_alloc(), ref);
            size_t n = link_list.size();
            for (size_t j = 0; j < n; ++j) {
                uint_fast64_t value = link_list.get_uint(j);
                size_t target_row_ndx = to_size_t(value);
                m_backlink_column->update_backlink(target_row_ndx, old_origin_row_ndx, new_origin_row_ndx); // Throws
            }
        }
    }

    LinkColumnBase::erase_rows(row_ndx, num_rows_to_erase, prior_num_rows, broken_reciprocal_backlinks); // Throws

    const bool fix_ndx_in_parent = true;
    adj_erase_rows<fix_ndx_in_parent>(row_ndx, num_rows_to_erase);
}


void LinkListColumn::move_last_row_over(size_t row_ndx, size_t prior_num_rows, bool broken_reciprocal_backlinks)
{
    REALM_ASSERT_DEBUG(prior_num_rows == size());
    REALM_ASSERT(row_ndx <= prior_num_rows);

    // Remove backlinks to the removed origin row
    if (ref_type ref = get_as_ref(row_ndx)) {
        if (!broken_reciprocal_backlinks) {
            IntegerColumn link_list(get_alloc(), ref);
            size_t n = link_list.size();
            for (size_t i = 0; i < n; ++i) {
                size_t target_row_ndx = to_size_t(link_list.get(i));
                m_backlink_column->remove_one_backlink(target_row_ndx, row_ndx);
            }
        }
        Array::destroy_deep(ref, get_alloc());
    }

    // Update backlinks to the moved origin row
    size_t last_row_ndx = prior_num_rows - 1;
    if (row_ndx != last_row_ndx) {
        if (ref_type ref = get_as_ref(last_row_ndx)) {
            IntegerColumn link_list(get_alloc(), ref);
            size_t n = link_list.size();
            for (size_t i = 0; i < n; ++i) {
                size_t target_row_ndx = to_size_t(link_list.get(i));
                m_backlink_column->update_backlink(target_row_ndx, last_row_ndx, row_ndx);
            }
        }
    }

    // Do the actual delete and move
    LinkColumnBase::move_last_row_over(row_ndx, prior_num_rows, broken_reciprocal_backlinks); // Throws

    const bool fix_ndx_in_parent = true;
    adj_move_over<fix_ndx_in_parent>(last_row_ndx, row_ndx);
}


void LinkListColumn::swap_rows(size_t row_ndx_1, size_t row_ndx_2)
{
    REALM_ASSERT_DEBUG(row_ndx_1 != row_ndx_2);

    // For swap, we have to make sure that we only update backlinks
    // once per target row. Otherwise, a linklist containing two
    // references to the same row would be swapped back, cancelling
    // out the effect of swap.
    // FIXME: This is unnecessarily slow because it heap-allocates.
    std::set<size_t> update_target_backlinks;

    ref_type ref_1 = get_as_ref(row_ndx_1);
    ref_type ref_2 = get_as_ref(row_ndx_2);

    if (ref_1) {
        IntegerColumn link_list(get_alloc(), ref_1);
        size_t n = link_list.size();
        for (size_t i = 0; i < n; ++i) {
            size_t target_row_ndx = to_size_t(link_list.get(i));
            update_target_backlinks.insert(target_row_ndx);
        }
    }

    if (ref_2) {
        IntegerColumn link_list(get_alloc(), ref_2);
        size_t n = link_list.size();
        for (size_t i = 0; i < n; ++i) {
            size_t target_row_ndx = to_size_t(link_list.get(i));
            update_target_backlinks.insert(target_row_ndx);
        }
    }

    for (size_t target_row : update_target_backlinks) {
        m_backlink_column->swap_backlinks(target_row, row_ndx_1, row_ndx_2);
    }

    IntegerColumn::swap_rows(row_ndx_1, row_ndx_2);
    const bool fix_ndx_in_parent = true;
    adj_swap<fix_ndx_in_parent>(row_ndx_1, row_ndx_2);
}


void LinkListColumn::clear(size_t, bool broken_reciprocal_backlinks)
{
    if (!broken_reciprocal_backlinks) {
        size_t num_target_rows = m_target_table->size();
        m_backlink_column->remove_all_backlinks(num_target_rows); // Throws
    }

    // Do the actual deletion
    clear_without_updating_index(); // Throws
    //  FIXME: This one is needed because
    // IntegerColumn::clear_without_updating_index() forgets about the leaf
    // type. A better solution should probably be sought after.
    get_root_array()->set_type(Array::type_HasRefs); // Throws

    discard_child_accessors();
}


void LinkListColumn::cascade_break_backlinks_to(size_t row_ndx, CascadeState& state)
{
    if (row_ndx == state.stop_on_link_list_row_ndx && this == state.stop_on_link_list_column)
        return;

    // Avoid the construction of both a LinkView and a IntegerColumn instance,
    // since both would involve heap allocations.
    ref_type ref = get_as_ref(row_ndx);
    if (ref == 0)
        return;
    BpTreeNode root(get_alloc());
    root.init_from_ref(ref);

    if (!root.is_inner_bptree_node()) {
        cascade_break_backlinks_to__leaf(row_ndx, root, state); // Throws
        return;
    }

    Array leaf(get_alloc());
    size_t link_ndx = 0;
    size_t num_links = root.get_bptree_size();
    while (link_ndx < num_links) {
        std::pair<MemRef, size_t> p = root.get_bptree_leaf(link_ndx);
        MemRef leaf_mem = p.first;
        leaf.init_from_mem(leaf_mem);
        cascade_break_backlinks_to__leaf(row_ndx, leaf, state); // Throws
        link_ndx += leaf.size();
    }
}


void LinkListColumn::cascade_break_backlinks_to__leaf(size_t row_ndx, const Array& link_list_leaf,
                                                      CascadeState& state)
{
    size_t target_table_ndx = m_target_table->get_index_in_group();

    size_t num_links = link_list_leaf.size();
    for (size_t i = 0; i < num_links; ++i) {
        size_t target_row_ndx = to_size_t(link_list_leaf.get(i));

        // Remove the reciprocal backlink at target_row_ndx that points to row_ndx
        m_backlink_column->remove_one_backlink(target_row_ndx, row_ndx);

        if (m_weak_links && state.only_strong_links)
            continue;
        if (m_target_table == state.stop_on_table)
            continue;

        // Recurse on target row when appropriate
        check_cascade_break_backlinks_to(target_table_ndx, target_row_ndx, state); // Throws
    }
}


void LinkListColumn::cascade_break_backlinks_to_all_rows(size_t num_rows, CascadeState& state)
{
    size_t num_target_rows = m_target_table->size();
    m_backlink_column->remove_all_backlinks(num_target_rows);

    if (m_weak_links)
        return;
    if (m_target_table == state.stop_on_table)
        return;

    // Avoid the construction of both a LinkView and a IntegerColumn instance,
    // since both would involve heap allocations.
    BpTreeNode root(get_alloc()), leaf(get_alloc());
    for (size_t i = 0; i < num_rows; ++i) {
        ref_type ref = get_as_ref(i);
        if (ref == 0)
            continue;
        root.init_from_ref(ref);

        if (!root.is_inner_bptree_node()) {
            cascade_break_backlinks_to_all_rows__leaf(root, state); // Throws
            continue;
        }

        size_t link_ndx = 0;
        size_t num_links = root.get_bptree_size();
        while (link_ndx < num_links) {
            std::pair<MemRef, size_t> p = root.get_bptree_leaf(link_ndx);
            MemRef leaf_mem = p.first;
            leaf.init_from_mem(leaf_mem);
            cascade_break_backlinks_to_all_rows__leaf(leaf, state); // Throws
            link_ndx += leaf.size();
        }
    }
}


void LinkListColumn::cascade_break_backlinks_to_all_rows__leaf(const Array& link_list_leaf, CascadeState& state)
{
    size_t target_table_ndx = m_target_table->get_index_in_group();

    size_t num_links = link_list_leaf.size();
    for (size_t i = 0; i < num_links; ++i) {
        size_t target_row_ndx = to_size_t(link_list_leaf.get(i));

        // Recurse on target row when appropriate
        check_cascade_break_backlinks_to(target_table_ndx, target_row_ndx, state); // Throws
    }
}


bool LinkListColumn::compare_link_list(const LinkListColumn& c) const
{
    size_t n = size();
    if (c.size() != n)
        return false;
    for (size_t i = 0; i < n; ++i) {
        if (*get(i) != *c.get(i))
            return false;
    }
    return true;
}


void LinkListColumn::do_nullify_link(size_t row_ndx, size_t old_target_row_ndx)
{
    LinkViewRef links = get(row_ndx);
    links->do_nullify_link(old_target_row_ndx);
}


void LinkListColumn::do_update_link(size_t row_ndx, size_t old_target_row_ndx, size_t new_target_row_ndx)
{
    LinkViewRef links = get(row_ndx);
    links->do_update_link(old_target_row_ndx, new_target_row_ndx);
}

void LinkListColumn::do_swap_link(size_t row_ndx, size_t target_row_ndx_1, size_t target_row_ndx_2)
{
    LinkViewRef links = get(row_ndx);
    links->do_swap_link(target_row_ndx_1, target_row_ndx_2);
}

void LinkListColumn::unregister_linkview()
{
    m_list_accessors_contains_tombstones = true;
}


LinkViewRef LinkListColumn::get_ptr(size_t row_ndx) const
{
    REALM_ASSERT_3(row_ndx, <, size());
    validate_list_accessors();

    auto create_view = [this, row_ndx](list_entry& entry) {
        entry.m_row_ndx = row_ndx;
        auto ptr = LinkView::create(m_table, const_cast<LinkListColumn&>(*this), row_ndx); // Throws
        entry.m_list = ptr;
        return ptr;
    };

    // Check if we already have a LinkView for this row.
    list_entry key;
    key.m_row_ndx = row_ndx;
    auto it = std::lower_bound(m_list_accessors.begin(), m_list_accessors.end(), key);
    if (it != m_list_accessors.end()) {
        if (it->m_row_ndx == row_ndx) {
            // If we have an existing LinkView, return it.
            if (LinkViewRef list = it->m_list.lock()) {
                REALM_ASSERT_DEBUG(list->is_attached());
                return list;
            }
        }
        if (it->m_list.expired()) {
            // We found an expired entry at the appropriate position. Reuse it with a new LinkView.
            return create_view(*it);
        }
    }

    // No existing entry for this row. If the entry prior to the insertion point has expired we can reuse it
    // as doing so preserves the desired ordering of m_list_accessors.
    if (it != m_list_accessors.begin()) {
        auto previous = std::prev(it);
        if (previous->m_list.expired()) {
            // We found an expired entry at the previous position. Reuse it with a new LinkView.
            return create_view(*previous);
        }
    }

    // Could not find an entry to reuse, so insert a new one.
    it = m_list_accessors.insert(it, std::move(key)); // Throws
    return create_view(*it);
}

void LinkListColumn::update_child_ref(size_t child_ndx, ref_type new_ref)
{
    LinkColumnBase::set(child_ndx, new_ref);
}


ref_type LinkListColumn::get_child_ref(size_t child_ndx) const noexcept
{
    return LinkColumnBase::get_as_ref(child_ndx);
}


void LinkListColumn::to_json_row(size_t row_ndx, std::ostream& out) const
{
    LinkViewRef links1 = const_cast<LinkListColumn*>(this)->get(row_ndx);
    for (size_t t = 0; t < links1->size(); t++) {
        if (t > 0)
            out << ", ";
        size_t target = links1->get(t).get_index();
        out << target;
    }
}


void LinkListColumn::discard_child_accessors() noexcept
{
    validate_list_accessors();
    for (auto& entry : m_list_accessors) {
        if (LinkViewRef list = entry.m_list.lock())
            list->detach();
    }
    m_list_accessors.clear();
}


void LinkListColumn::refresh_accessor_tree(size_t col_ndx, const Spec& spec)
{
    prune_list_accessor_tombstones();

    LinkColumnBase::refresh_accessor_tree(col_ndx, spec); // Throws
    for (auto& entry : m_list_accessors) {
        if (LinkViewRef list = entry.m_list.lock())
            list->refresh_accessor_tree(entry.m_row_ndx);
    }
}


void LinkListColumn::adj_acc_insert_rows(size_t row_ndx, size_t num_rows_inserted) noexcept
{
    LinkColumnBase::adj_acc_insert_rows(row_ndx, num_rows_inserted);

    const bool fix_ndx_in_parent = false;
    adj_insert_rows<fix_ndx_in_parent>(row_ndx, num_rows_inserted);
}


void LinkListColumn::adj_acc_erase_row(size_t row_ndx) noexcept
{
    LinkColumnBase::adj_acc_erase_row(row_ndx);

    const bool fix_ndx_in_parent = false;
    size_t num_rows_erased = 1;
    adj_erase_rows<fix_ndx_in_parent>(row_ndx, num_rows_erased);
}


void LinkListColumn::adj_acc_move_over(size_t from_row_ndx, size_t to_row_ndx) noexcept
{
    LinkColumnBase::adj_acc_move_over(from_row_ndx, to_row_ndx);

    const bool fix_ndx_in_parent = false;
    adj_move_over<fix_ndx_in_parent>(from_row_ndx, to_row_ndx);
}


void LinkListColumn::adj_acc_swap_rows(size_t row_ndx_1, size_t row_ndx_2) noexcept
{
    LinkColumnBase::adj_acc_swap_rows(row_ndx_1, row_ndx_2);

    const bool fix_ndx_in_parent = false;
    adj_swap<fix_ndx_in_parent>(row_ndx_1, row_ndx_2);
}


void LinkListColumn::adj_acc_merge_rows(size_t old_row_ndx, size_t new_row_ndx) noexcept
{
    prune_list_accessor_tombstones();

    auto begin = m_list_accessors.begin(), end = m_list_accessors.end();
    auto old_it = std::lower_bound(begin, end, list_entry{old_row_ndx, std::weak_ptr<LinkView>()});
    if (old_it == end || old_it->m_row_ndx != old_row_ndx)
        return;

    // move the accessor to the correct position in the sorted list for the new value
    if (old_row_ndx < new_row_ndx) {
        auto new_it = std::lower_bound(old_it, end, list_entry{new_row_ndx, std::weak_ptr<LinkView>()});
        std::rotate(old_it, old_it + 1, new_it);
        old_it = new_it - 1;
    }
    else {
        auto new_it = std::lower_bound(begin, old_it, list_entry{new_row_ndx, std::weak_ptr<LinkView>()});
        std::rotate(new_it, old_it, old_it + 1);
        old_it = new_it;
    }

    // update the accessor
    old_it->m_row_ndx = new_row_ndx;
    if (LinkViewRef list = old_it->m_list.lock())
        list->set_origin_row_index(new_row_ndx);

    validate_list_accessors();
}


template <bool fix_ndx_in_parent>
void LinkListColumn::adj_insert_rows(size_t row_ndx, size_t num_rows_inserted) noexcept
{
    prune_list_accessor_tombstones();

    auto end = m_list_accessors.end();
    auto it = std::lower_bound(m_list_accessors.begin(), end, list_entry{row_ndx, std::weak_ptr<LinkView>()});
    for (; it != end; ++it) {
        it->m_row_ndx += num_rows_inserted;
        if (fix_ndx_in_parent) {
            if (LinkViewRef list = it->m_list.lock())
                list->set_origin_row_index(it->m_row_ndx);
        }
    }

    validate_list_accessors();
}


template <bool fix_ndx_in_parent>
void LinkListColumn::adj_erase_rows(size_t row_ndx, size_t num_rows_erased) noexcept
{
    prune_list_accessor_tombstones();

    auto end = m_list_accessors.end();
    auto erased_begin =
        std::lower_bound(m_list_accessors.begin(), end, list_entry{row_ndx, std::weak_ptr<LinkView>()});
    auto erased_end =
        std::lower_bound(erased_begin, end, list_entry{row_ndx + num_rows_erased, std::weak_ptr<LinkView>()});

    for (auto it = erased_begin; it != erased_end; ++it) {
        if (LinkViewRef list = it->m_list.lock())
            list->detach();
    }

    for (auto it = erased_end; it != end; ++it) {
        it->m_row_ndx -= num_rows_erased;
        if (fix_ndx_in_parent) {
            if (LinkViewRef list = it->m_list.lock())
                list->set_origin_row_index(it->m_row_ndx);
        }
    }

    m_list_accessors.erase(erased_begin, erased_end);

    validate_list_accessors();
}


template <bool fix_ndx_in_parent>
void LinkListColumn::adj_move_over(size_t from_row_ndx, size_t to_row_ndx) noexcept
{
    prune_list_accessor_tombstones();

    auto begin = m_list_accessors.begin();
    auto end = m_list_accessors.end();

    bool to_is_valid = false;
    auto to = std::lower_bound(begin, end, list_entry{to_row_ndx, std::weak_ptr<LinkView>()});
    if (to != end && to->m_row_ndx == to_row_ndx) {
        to_is_valid = true;

        if (LinkViewRef list = to->m_list.lock()) {
            list->detach();
            to->m_list.reset();
            m_list_accessors_contains_tombstones = true;
        }
    }
    if (from_row_ndx == to_row_ndx) {
        validate_list_accessors();
        return;
    }

    auto from = std::lower_bound(begin, end, list_entry{from_row_ndx, std::weak_ptr<LinkView>()});
    if (from != end && from->m_row_ndx == from_row_ndx) {
        from->m_row_ndx = to_row_ndx;
        if (fix_ndx_in_parent) {
            if (LinkViewRef list = from->m_list.lock())
                list->set_origin_row_index(to_row_ndx);
        }

        if (to_is_valid) {
            to->m_row_ndx = from_row_ndx;
            std::iter_swap(to, from);
        }
        else if (from < to) {
            std::rotate(from, from + 1, to);
        }
        else {
            std::rotate(to, from, from + 1);
        }
    }

    validate_list_accessors();
}


template <bool fix_ndx_in_parent>
void LinkListColumn::adj_swap(size_t row_ndx_1, size_t row_ndx_2) noexcept
{
    prune_list_accessor_tombstones();

    auto begin = m_list_accessors.begin();
    auto end = m_list_accessors.end();

    auto it_1 = std::lower_bound(begin, end, list_entry{row_ndx_1, std::weak_ptr<LinkView>()});
    bool row_1_found = (it_1 != end && it_1->m_row_ndx == row_ndx_1);
    LinkViewRef ptr_1;
    if (row_1_found) {
        ptr_1 = it_1->m_list.lock();
        if (!bool(ptr_1))
            row_1_found = false;
    }
    auto it_2 = std::lower_bound(begin, end, list_entry{row_ndx_2, std::weak_ptr<LinkView>()});
    bool row_2_found = (it_2 != end && it_2->m_row_ndx == row_ndx_2);
    LinkViewRef ptr_2;
    if (row_2_found) {
        ptr_2 = it_2->m_list.lock();
        if (!bool(ptr_2))
            row_2_found = false;
    }
    if (row_1_found && row_2_found) {
        if (fix_ndx_in_parent) {
            ptr_1->set_origin_row_index(row_ndx_2);
            ptr_2->set_origin_row_index(row_ndx_1);
        }
        std::swap(it_1->m_list, it_2->m_list);
    }
    else if (row_1_found || row_2_found) {
        auto single = end;
        auto remainder = end;

        if (row_1_found) {
            it_1->m_row_ndx = row_ndx_2;
            if (fix_ndx_in_parent)
                ptr_1->set_origin_row_index(row_ndx_2);

            single = it_1;
            remainder = it_2;
        }
        else {
            it_2->m_row_ndx = row_ndx_1;
            if (fix_ndx_in_parent)
                ptr_2->set_origin_row_index(row_ndx_1);

            single = it_2;
            remainder = it_1;
        }

        if (single < remainder)
            std::rotate(single, single + 1, remainder);
        else
            std::rotate(remainder, single, single + 1);
    }

    validate_list_accessors();
}


void LinkListColumn::adj_acc_clear_root_table() noexcept
{
    LinkColumnBase::adj_acc_clear_root_table();
    discard_child_accessors();
}


void LinkListColumn::update_from_parent(size_t old_baseline) noexcept
{
    if (!get_root_array()->update_from_parent(old_baseline))
        return;

    prune_list_accessor_tombstones();

    for (auto& list_accessor : m_list_accessors) {
        if (LinkViewRef list = list_accessor.m_list.lock())
            list->update_from_parent(old_baseline);
    }
}


void LinkListColumn::validate_list_accessors() const noexcept
{
#ifdef REALM_DEBUG
    auto begin = m_list_accessors.begin();
    auto end = m_list_accessors.end();
    REALM_ASSERT_DEBUG(std::is_sorted(begin, end));
    REALM_ASSERT_DEBUG(end == std::adjacent_find(begin, end, [](const list_entry& a, const list_entry& b) {
                           return a.m_row_ndx == b.m_row_ndx;
                       }));
#endif
}


void LinkListColumn::prune_list_accessor_tombstones() noexcept
{
    validate_list_accessors();
    bool had_tombstones = m_list_accessors_contains_tombstones.exchange(false);
    if (!had_tombstones)
        return;
    // While we scan through and remove tombstones, new one may be generated.
    // this is ok, because it does not actually change the list. Tombstones are
    // represented by expired weak_ptrs. This also implies, that after a call
    // to prune_list_accessor_tombstones() there is *no* guarantee that all tombstones
    // have been removed. It is merely a best effort at reducing the size of the
    // vector.
    auto remove_from = std::remove_if(m_list_accessors.begin(), m_list_accessors.end(),
                                      [](const list_entry& e) { return e.m_list.expired(); });
    m_list_accessors.erase(remove_from, m_list_accessors.end());
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

void LinkListColumn::verify() const
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


void LinkListColumn::verify(const Table& table, size_t col_ndx) const
{
#ifdef REALM_DEBUG
    LinkColumnBase::verify(table, col_ndx);

    std::vector<BacklinkColumn::VerifyPair> pairs;
    m_backlink_column->get_backlinks(pairs);

    // For each link list, verify the accessor, then check that the contents of
    // the list is in agreement with the corresponding backlinks. A forward link
    // (origin_row_ndx -> target_row_ndx) with multiplicity N must exists if,
    // and only if there exists a backward link (target_row_ndx ->
    // origin_row_ndx) with multiplicity N.
    size_t backlinks_seen = 0;
    size_t n = size();
    for (size_t i = 0; i != n; ++i) {
        ConstLinkViewRef link_list = get(i);
        link_list->verify(i);
        std::multiset<size_t> links_1, links_2;
        size_t m = link_list->size();
        for (size_t j = 0; j < m; ++j)
            links_1.insert(link_list->get(j).get_index());
        typedef std::vector<BacklinkColumn::VerifyPair>::const_iterator iter;
        BacklinkColumn::VerifyPair search_value;
        search_value.origin_row_ndx = i;
        std::pair<iter, iter> range = equal_range(pairs.begin(), pairs.end(), search_value);
        for (iter j = range.first; j != range.second; ++j)
            links_2.insert(j->target_row_ndx);
        REALM_ASSERT(links_1 == links_2);
        backlinks_seen += links_2.size();
    }

    // All backlinks must have been matched by a forward link
    REALM_ASSERT_3(backlinks_seen, ==, pairs.size());
#else
    static_cast<void>(table);
    static_cast<void>(col_ndx);
#endif
}


std::pair<ref_type, size_t> LinkListColumn::get_to_dot_parent(size_t ndx_in_parent) const
{
    return IntegerColumn::get_to_dot_parent(ndx_in_parent);
}

// LCOV_EXCL_STOP ignore debug functions
