/************************************************************************************
* Copyright (c) 2018 ONVIF.
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above copyright
*      notice, this list of conditions and the following disclaimer in the
*      documentation and/or other materials provided with the distribution.
*    * Neither the name of ONVIF nor the names of its contributors may be
*      used to endorse or promote products derived from this software
*      without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL ONVIF BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
************************************************************************************/
#ifndef SISO_INCLUDED
#include <stdio.h>
#include <stdint.h>
#define SISO_INCLUDED
namespace siso {
	/**
	 * Class for simple ISO Base File Format box manipulation.
	 * The class can also be used to read and write the similar QuickTime atoms.
	 */
	class box {
	public:
		/**
		 * Open file as root box.
		 * Do not delete this instance while reading or writing the file.
		 */
		box(const char *path) {
			fd = fopen(path, "rb+");
			m_tag = 0;
			m_size = m_offset = 0;
			parent = 0;
			if (fd) {
				fseek(fd, 0, 2);
				m_size = ftell(fd);
			}
		}
		~box() { if (fd && m_tag == 0) fclose(fd); }
		/**
		 * Retrieve first child. Optionally search for a type. 
		 * @param tag If present search for next box with given tag in host order. Pass e.g. 'moov'.
		 * @param offset For Full boxes pass 4 as extra offset to first child.
		 */
		box first(uint32_t tag = 0, size_t offset= 0) const {
			size_t o = m_tag ? m_offset + 8 + offset : 0;		// file has no header
			if (o < end()) {
				for (box b = box(o, this); b; b = b.next()) if (tag == 0 || b.m_tag == tag) return b;
			}
			return box();
		}
		/**
		 * Get next sibling.
		 * @param Search for next box with given tag in host order. Pass e.g. 'moov'.
		 */
		box next(uint32_t tag) {
			for (box b=next(); b; b = b.next()) if (b.m_tag == tag) return b;
			return box();
		}
		/**
		* Get next sibling.
		*/
		box next() { return end() < parent->end() ? box(end(), parent) : box(); }
		/**
		 * Append child optionally including data. 
		 * Works only for last item in file
		 * @param tag 4 character box type in host order. Use e.g. 'meta'.
		 * @param payloadSize Size of the box payload without eight byte header.
		 * @param data Optional pointer to payload to be copied.
		 */
		 box append(uint32_t tag, size_t payloadSize = 0, const void *data = 0) {
			box b(tag, payloadSize + 8, m_offset + m_size, this);
			b.writeHeader();
			if (payloadSize) write(b.m_offset + 8, data, payloadSize);
			resize(payloadSize + 8);
			return b;
		}
		/** 
		 * Append data. Works only for last item in file
		 * @param data Pointer to buffer or zero for filling with zeros.
		 * @param bytes Number of bytes to append.
		 * @return 0 on OK
		 */
		int append(const void *data, size_t bytes) {
			write(m_offset + m_size, data, bytes);
			return resize(bytes);
		}
		/** 
		 * Update part or all data of a box. 
		 * @param offset Zero points right after 8 byte header.
		 * @param data Pointer to buffer or zero for filling with zeros.
		 * @param bytes Number of bytes to append.
		 * @return 0 on OK
		 */
		int update(size_t offset, const void *data, size_t bytes) {
			if (offset + 8 + bytes > m_size) return -1;
			return write(m_offset + 8 + offset, data, bytes);
		}
		/**
		* Read part or all data of a box.
		* @param offset Zero points right after 8 byte header.
		* @param data Pointer to buffer. Provide at least size of parameter bytes.
		* @param bytes Number of bytes to copy into buffer.
		* @return Number of bytes copied
		*/
		size_t read(size_t offset, void *data, size_t bytes) {
			if (offset + 8 >= m_size) return 0;
			if (bytes > m_size - 8) bytes = m_size - 8;
			fseek(fd, m_offset + offset + 8, 0);
			return fread(data, 1, bytes, fd);
		}
		/// Return whether box exists.
		operator bool() const { return m_size != 0; }
		/// Support array adressing of boxes
		box operator [](int type) const { return first(type); }
		/// Return whether this box is last in file
		bool isLast() const { 
			const box *file = this; while (file->parent) file = file->parent; 
			return m_offset + m_size == file->m_size;
		}
		/// Flush all data to disk.
		void flush() { fflush(fd); }
		/// Helper to swap data from little to big endian
		static uint32_t swap32(uint32_t x) {
			return ((x & 0xff) << 24) | ((x & 0xff00) << 8) | ((x & 0xff0000) >> 8) | ((x & 0xff000000) >> 24);
		}
		/// Helper to swap data from little to big endian
		static uint64_t swap64(uint64_t x) {
			union { uint64_t a; uint32_t b[2]; } u;
			u.a = x;
			return ((uint64_t)swap32(u.b[0]) << 32) | swap32(u.b[1]);
		}
	private:
		box() { *this = box(0, 0, 0, 0); }
		box(int _tag, size_t _size, size_t _offset, box *_parent) {
			m_tag = _tag; m_size = _size; m_offset = _offset; parent = _parent; fd = _parent ? _parent->fd : 0;
		}
		box(size_t offset, const box *parent) {
			uint32_t h[2];
			fseek(parent->fd, offset, 0);
			fread(h, 1, 8, parent->fd);
			*this = box(swap32(h[1]), swap32(h[0]), offset, (box*)parent);
		}
		void writeHeader() {
			uint32_t h[2] = { swap32(m_size), swap32(m_tag) };
			write(m_offset, h, 8);
		}
		/// Recursively enlarge this box and all its parents boxes to fit new content
		int resize(size_t bytes) {
			if (m_tag == 0) return 0;		// nothing to be done for file
			m_size += bytes;
			uint32_t s = swap32(m_size);
			if (write(m_offset, &s, 4)) return -1;
			if (parent) return parent->resize(bytes);
			return 0;
		}
		int write(size_t offset, const void *data, size_t bytes) {
			if (data == 0) return clear(offset, bytes);
			fseek(fd, offset, 0);
			return fwrite(data, 1, bytes, fd) == bytes ? 0 : -1;
		}
		int clear(size_t offset, size_t bytes) {
			fseek(fd, offset, 0);
			static char zero[32];
			for (; bytes > sizeof(zero); bytes -= sizeof(zero)) fwrite(zero, 1, sizeof(zero), fd);
			return fwrite(zero, 1, bytes, fd) == bytes ? 0 : -1;
		}
		size_t end() const { return m_offset + m_size; }
		FILE *fd;			///< File descriptor for IO operations
		box *parent;		///< Parent node
		size_t m_size;		///< Size of the box including headers
		size_t m_offset;	///< Offset of the box header in the file
		uint32_t m_tag;		///< Box type code in host order
	};

}
#endif