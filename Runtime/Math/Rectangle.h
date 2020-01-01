/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES =====================
#include <memory>
#include "../RHI/RHI_Definition.h"
//================================

namespace Spartan
{
	class Renderer;

	namespace Math
	{
		class Rectangle
		{
		public:
			Rectangle()
			{
				x		= 0;
				y		= 0;
				width	= 0;
				height	= 0;
			}

			Rectangle(const float x, const float y, const float width, const float height)
			{
				this->x			= x;
				this->y			= y;
				this->width		= width;
				this->height	= height;
			}

			Rectangle(const Rectangle& rectangle)
			{
				x		= rectangle.x;
				y		= rectangle.y;
				width	= rectangle.width;
				height	= rectangle.height;
			}

			~Rectangle() = default;

			bool operator==(const Rectangle& rhs) const
			{
				return
					x		== rhs.x &&
					y		== rhs.y &&
					width	== rhs.width &&
					height	== rhs.height;
			}

			bool operator!=(const Rectangle& rhs) const
			{
				return
					x		!= rhs.x        ||
					y		!= rhs.y        ||
					width	!= rhs.width    ||
					height	!= rhs.height;
			}

            bool IsDefined() const
            {
                return  x       != 0.0f ||
                        y       != 0.0f ||
                        width   != 0.0f ||
                        height  != 0.0f;
            }

			bool CreateBuffers(Renderer* renderer);
			static int GetIndexCount()			{ return 6; }
			const auto&	GetIndexBuffer() const	{ return m_indexBuffer; }
			const auto& GetVertexBuffer() const	{ return m_vertexBuffer; }

			float x;
			float y;
			float width;
			float height;

		private:
			std::shared_ptr<RHI_VertexBuffer> m_vertexBuffer;
			std::shared_ptr<RHI_IndexBuffer> m_indexBuffer;
		};
	}
}
