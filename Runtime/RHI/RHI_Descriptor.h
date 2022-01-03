/*
Copyright(c) 2016-2022 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

//= INCLUDES =================
#include "RHI_Definition.h"
#include "../Utilities/Hash.h"
//============================

namespace Spartan
{
    struct RHI_Descriptor
    {
        RHI_Descriptor() = default;

        RHI_Descriptor(const RHI_Descriptor& descriptor)
        {
            type                        = descriptor.type;
            layout                      = descriptor.layout;
            slot                        = descriptor.slot;
            stage                       = descriptor.stage;
            is_dynamic_constant_buffer  = descriptor.is_dynamic_constant_buffer;
            name                        = descriptor.name;
            mip                         = descriptor.mip;
            array_size                  = descriptor.array_size;
        }

        RHI_Descriptor(const std::string& name, const RHI_Descriptor_Type type, const RHI_Image_Layout layout, const uint32_t slot, const uint32_t array_size, const uint32_t stage, const bool is_storage, const bool is_dynamic_constant_buffer)
        {
            this->type                          = type;
            this->layout                        = layout;
            this->slot                          = slot;
            this->stage                         = stage;
            this->is_dynamic_constant_buffer    = is_dynamic_constant_buffer;
            this->name                          = name;
            this->array_size                    = array_size;
        }

        uint32_t ComputeHash(bool include_data) const
        {
            uint32_t hash = 0;

            Utility::Hash::hash_combine(hash, slot);
            Utility::Hash::hash_combine(hash, stage);
            Utility::Hash::hash_combine(hash, offset);
            Utility::Hash::hash_combine(hash, range);
            Utility::Hash::hash_combine(hash, is_dynamic_constant_buffer);
            Utility::Hash::hash_combine(hash, static_cast<uint32_t>(type));
            Utility::Hash::hash_combine(hash, static_cast<uint32_t>(layout));

            if (include_data)
            {
                Utility::Hash::hash_combine(hash, data);
                Utility::Hash::hash_combine(hash, mip);
            }

            return hash;
        }

        bool IsStorage() const { return type == RHI_Descriptor_Type::TextureStorage; }

        uint32_t slot                   = 0;
        uint32_t stage                  = 0;
        uint64_t offset                 = 0;
        uint64_t range                  = 0;
        RHI_Descriptor_Type type        = RHI_Descriptor_Type::Undefined;
        RHI_Image_Layout layout         = RHI_Image_Layout::Undefined;
        bool is_dynamic_constant_buffer = false;
        uint32_t array_size             = 0;

        // Data
        int mip     = -1;
        void* data  = nullptr;

        // Misc
        std::string name;
    };
}
