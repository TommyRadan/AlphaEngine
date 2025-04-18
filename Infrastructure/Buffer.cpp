/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
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

#include <Infrastructure/Buffer.hpp>
#include <fstream>

#include <Infrastructure/log.hpp>

Infrastructure::Buffer::Buffer(const std::string& filename)
{
    std::ifstream in(filename, std::ios::in | std::ios::binary);

    if (!in)
    {
        LOG_ERR("Could not load buffer (%s)", filename.c_str());
        return;
    }

    in.seekg(0, std::ios::end);
    m_Data.resize(static_cast<size_t>(in.tellg()));
    in.seekg(0, std::ios::beg);

    in.read((char*) m_Data.data(), m_Data.size());

    in.close();

    LOG_INF("Loaded buffer (%s)", filename.c_str());
}

const uint8_t* Infrastructure::Buffer::GetData() const
{
    return m_Data.data();
}
