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

	Copyright (c) Byteduck 2016-2021. All rights reserved.
*/

#include <utility>
#include <vector>
#include <string>
#include <functional>
#include <sys/socketfs.h>
#include "packet.h"
#include <cstring>
#include "BusConnection.h"
#include "serialization_utils.h"

#pragma once

namespace River {

	class IFunction {
	public:
		virtual void remote_call(const RiverPacket& packet) = 0;
		virtual const std::string& path() = 0;
	};

	template<typename RetT, typename... ParamTs>
	class Function: public IFunction {
		static_assert(is_valid_return_type<RetT>(), "Function return type must be a plain-old datatype, vector, or void!");
		static_assert((is_valid_param_type<ParamTs>() && ...), "Function arguments must be plain-old datatypes or vectors!");

	public:
		Function(const std::string& path): _path(path), _endpoint(nullptr), _callback(nullptr) {}

		Function(const std::string& path, std::shared_ptr<Endpoint> endpoint, std::function<RetT(sockid_t, ParamTs...)> callback = nullptr):
				_path(stringname_of(path)),
				_endpoint(std::move(endpoint)),
				_callback(callback) {}

		static std::string stringname_of(const std::string& path) {
			std::string ret = path + "<" + typeid(RetT).name() + "[";
			if constexpr(std::is_void<RetT>())
				ret += "0]";
			else
				ret += std::to_string(sizeof(RetT)) + "]";
			((ret += std::string(",") + typeid(ParamTs).name() + "[" + std::to_string(sizeof(ParamTs)) + "]"), ...);
			return ret + ">";
		}

		RetT operator()(ParamTs... args) const {
			if(!_endpoint) {
				Duck::Log::err("[River] Tried calling uninitialized function ", _path);
				return RetT();
			}

			if(_endpoint->type() == Endpoint::PROXY) {
				RiverPacket packet = {
						FUNCTION_CALL,
						_endpoint->name(),
						_path
				};

				//Serialize function call data (tuple {arg1, arg2, arg3...})
				packet.data_length = buffer_size(args...);
				auto buffer_res = Duck::SharedBuffer::create(packet.data_length);
				if(buffer_res.is_error()) {
					Duck::Log::errf("[River] Couldn't allocate buffer for function call: {}", buffer_res.result());
					return RetT();
				}
				packet.data = buffer_res.value();
				serialize((uint8_t*) packet.data.value().ptr(), args...);

				//Send the function call packet and await a reply (if the function has a non-void return type)
				_endpoint->bus()->send_packet(packet);
				if constexpr(!std::is_void<RetT>()) {
					auto pkt = _endpoint->bus()->await_packet(FUNCTION_RETURN, _endpoint->name(), _path);
					if(pkt.error || !pkt.data.has_value()) {
						Duck::Log::err("[River] Remote function call ", pkt.endpoint, ":", pkt.path, " failed: ", error_str(pkt.error));
						if constexpr(!std::is_void<RetT>())
							return RetT();
					}

					//Deserialize and return the return value TODO: Check size
					RetT ret;
					deserialize((uint8_t*) pkt.data.value().ptr(), ret);
					return ret;
				}
			} else {
				return _callback(0, args...);
			}
		}

		const std::string& path() override {
			return _path;
		}

		void remote_call(const RiverPacket& packet) override {
			//TODO Make sure the data is the correct size
			if(/*packet.data.size() != buffer_size(ParamTs)*/ !packet.data.has_value()) {
				_endpoint->bus()->send_packet({
					FUNCTION_RETURN,
					packet.endpoint,
					packet.path,
					MALFORMED_DATA,
					packet.sender
				});
			}

			//Deserialize the parameters
			std::tuple<ParamTs...> data_tuple;
			deserialize((uint8_t*) packet.data.value().ptr(), std::get<ParamTs>(data_tuple)...);

			//Call the function
			if constexpr(!std::is_void<RetT>()) {
				//Serialize the return value and send the response
				RiverPacket resp {
						FUNCTION_RETURN,
						packet.endpoint,
						packet.path
				};
				resp.recipient = packet.sender;
				RetT ret = _callback(packet.sender, std::get<ParamTs>(data_tuple)...);
				auto buffer_res = Duck::SharedBuffer::create(buffer_size(ret));
				if(buffer_res.is_error()) {
					Duck::Log::errf("[River] Couldn't allocate buffer for function return: {}", buffer_res.result());
					return;
				}
				resp.data = buffer_res.value();
				serialize((uint8_t*) resp.data.value().ptr(), ret);
				_endpoint->bus()->send_packet(resp);
			} else {
				_callback(packet.sender, std::get<ParamTs>(data_tuple)...);
			}
		}

	private:
		std::string _path;
		std::shared_ptr<Endpoint> _endpoint;
		std::function<RetT(sockid_t, ParamTs...)> _callback;
	};
}

