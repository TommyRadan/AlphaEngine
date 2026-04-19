/**
 * Copyright (c) 2015-2025 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <asset_manager/asset_manager.hpp>
#include <infrastructure/log.hpp>

void asset_manager::context::init()
{
    LOG_INF("Init Asset Manager");
}

void asset_manager::context::quit()
{
    // Count live vs. expired handles for a final diagnostic. The cache itself
    // is cleared either way — remaining shared_ptrs keep the assets alive
    // until their last holder drops them.
    std::size_t live = 0;
    std::size_t expired = 0;
    for (const auto& [type, bucket] : m_caches)
    {
        for (const auto& [path, weak] : bucket)
        {
            if (weak.expired())
            {
                ++expired;
            }
            else
            {
                ++live;
            }
        }
    }
    LOG_INF("Quit Asset Manager: %zu live handle(s), %zu expired entry(ies) tracked", live, expired);
    m_caches.clear();
}

std::size_t asset_manager::context::collect_unused()
{
    std::size_t pruned = 0;
    for (auto& [type, bucket] : m_caches)
    {
        for (auto it = bucket.begin(); it != bucket.end();)
        {
            if (it->second.expired())
            {
                it = bucket.erase(it);
                ++pruned;
            }
            else
            {
                ++it;
            }
        }
    }
    if (pruned > 0)
    {
        LOG_INF("Asset Manager collected %zu unused entry(ies)", pruned);
    }
    return pruned;
}
