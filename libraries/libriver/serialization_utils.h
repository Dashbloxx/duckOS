/*
    This file is part of duckOS.
    
    duckOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    
    duckOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    along with duckOS.  If not, see <https://www.gnu.org/licenses/>.
    
    Copyright (c) Byteduck 2016-2022. All rights reserved.
*/

#pragma once

#include <vector>
#include <libduck/Log.h>

namespace River {
	template<typename>
	struct is_vector : std::integral_constant<bool, false> {};

	template<typename T>
	struct is_vector<std::vector<T>> : std::integral_constant<bool, true> {};

	template<typename T>
	struct is_valid_param_type : std::integral_constant<bool, std::is_pod<T>() || is_vector<T>() || std::is_same<T, std::string>()> {};

	template<typename T>
	struct is_valid_return_type : std::integral_constant<bool, is_valid_param_type<T>() || std::is_void<T>()> {};

	constexpr size_t buffer_size() {
		return 0; //Base case
	}

	/**
	 * Calculates the size (in bytes) of the buffer needed to serialize the given parameters.
	 */
	template<typename ParamT, typename... ParamTs>
	constexpr size_t buffer_size(const ParamT& first, const ParamTs&... rest) {
		if constexpr(is_vector<ParamT>())
			return sizeof(size_t) + sizeof(typename ParamT::value_type) * first.size() + buffer_size(rest...);
		else if constexpr(std::is_same<ParamT, std::string>())
			return first.size() + 1;
		else
			return sizeof(ParamT) + buffer_size(rest...);
	}

	constexpr uint8_t* serialize(uint8_t* buf) {
		//base case
		return buf;
	}

	/**
	 * Serializes the parameters into the byte array given. Assumes correctly allocated memory of size calculated by
	 * buffer_size().
	 */
	template<typename ParamT, typename... ParamTs>
	constexpr uint8_t* serialize(uint8_t* buf, const ParamT& first, const ParamTs&... rest) {
		if constexpr(is_vector<ParamT>()) {
			typedef typename ParamT::value_type VecT;
			//If it's a vector, push a size_t of the size and then the data
			*((size_t*)buf) = first.size();
			buf += sizeof(size_t);
			for(const VecT& item : first)
				buf = serialize(buf, item);
		} else if constexpr(std::is_same<ParamT, std::string>()) {
			//If it's a string, we can just push the bytes of the string
			strcpy((char*) buf, first.data());
			buf += first.size() + 1;
		} else {
			*((ParamT*) buf) = first;
			buf += sizeof(ParamT);
		}

		return serialize(buf, rest...);
	}

	constexpr const uint8_t* deserialize(const uint8_t* buf) {
		//base case
		return buf;
	}

	/**
	 * Deserializes the parameters into from byte array given.
	 */
	template<typename ParamT, typename... ParamTs>
	constexpr const uint8_t* deserialize(const uint8_t* buf, ParamT& first, ParamTs&... rest) {
		if constexpr(is_vector<ParamT>()) {
			typedef typename ParamT::value_type VecT;
			//If it's a vector, pop a size_t of the size and then the data
			first.resize(*((size_t*)buf)); //TODO Sanity check?
			buf += sizeof(size_t);
			for(VecT& item : first)
				buf = deserialize(buf, item);
		} else if constexpr(std::is_same<ParamT, std::string>()) {
			//If it's a string, just copy from the buffer until we hit a null terminator
			first = std::string((char*)buf);
			buf += first.size() + 1;
		} else {
			//If it's not a vector, just push the data
			first = *((ParamT*) buf);
			buf += sizeof(ParamT);
		}

		return deserialize(buf, rest...);
	}
}
