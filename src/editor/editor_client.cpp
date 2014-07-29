#include "editor_client.h"
#include "core/array.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/fifo_allocator.h"
#include "core/MT/lock_free_queue.h"
#include "core/MT/mutex.h"
#include "core/MT/task.h"
#include "core/net/tcp_connector.h"
#include "core/net/tcp_stream.h"
#include "core/path.h"
#include "core/profiler.h"
#include "editor/client_message_types.h"
#include "editor/editor_server.h"
#include "editor/server_message_types.h"
#include "universe/universe.h"

namespace Lumix
{

	struct EditorClientImpl
	{
		EditorClientImpl(EditorServer& server)
			: m_server(server)
		{}

		void sendMessage(uint32_t type, const void* data, int32_t size);
		void onMessage(const uint8_t* data, int size);

		Path m_base_path;
		Path m_universe_path;
		EditorServer& m_server;

		DelegateList<void(EntityPositionEvent&)> m_entity_position_changed;
		DelegateList<void(EntitySelectedEvent&)> m_entity_selected;
		DelegateList <void(LogEvent&)> m_message_logged;
		DelegateList <void(PropertyListEvent&)> m_property_list_received;
	};


	bool EditorClient::create(const char* base_path, EditorServer& server)
	{
		m_impl = LUMIX_NEW(EditorClientImpl)(server);
		m_impl->m_base_path = base_path;
		return true;
	}

	void EditorClient::destroy()
	{
		if (m_impl)
		{
			LUMIX_DELETE(m_impl);
			m_impl = NULL;
		}
	}

	void EditorClient::onMessage(const uint8_t* data, int size)
	{
		if(m_impl)
		{
			m_impl->onMessage(data, size);
		}
	}

	void EditorClientImpl::onMessage(const uint8_t* data, int size)
	{
		Blob stream;
		stream.create(data, size);
		int32_t message_type;
		stream.read(message_type);
		switch(message_type)
		{
			case ServerMessageType::ENTITY_POSITION:
				{
					EntityPositionEvent msg;
					msg.read(stream);
					m_entity_position_changed.invoke(msg);
				}
				break;
			case ServerMessageType::ENTITY_SELECTED:
				{
					EntitySelectedEvent msg;
					msg.read(stream);
					m_entity_selected.invoke(msg);
				}
				break;
			case ServerMessageType::PROPERTY_LIST:
				{ 	
					PropertyListEvent msg;
					msg.read(stream);
					m_property_list_received.invoke(msg);
				}
				break;
			case ServerMessageType::LOG_MESSAGE:
				{
					LogEvent msg;
					msg.read(stream);
					m_message_logged.invoke(msg);
				}
				break;
			default:
				break;
		}
	}


	EditorClient::PropertyListCallback& EditorClient::propertyListReceived()
	{
		return m_impl->m_property_list_received;
	}


	EditorClient::EntitySelectedCallback& EditorClient::entitySelected()
	{
		return m_impl->m_entity_selected;
	}

	EditorClient::EntityPositionCallback& EditorClient::entityPositionReceived()
	{
		return m_impl->m_entity_position_changed;
	}

	void EditorClientImpl::sendMessage(uint32_t type, const void* data, int32_t size)
	{
		Blob stream;
		stream.write(&type, sizeof(type));
		if (data)
		{
			stream.write(data, size);
		}
		m_server.onMessage(stream.getBuffer(), stream.getBufferSize());
	}


	const char* EditorClient::getBasePath() const
	{
		return m_impl->m_base_path.c_str();
	}


	void EditorClient::lookAtSelected()
	{
		m_impl->sendMessage((uint32_t)ClientMessageType::LOOK_AT_SELECTED, NULL, 0);
	}


	void EditorClient::addComponent(uint32_t type)
	{
		m_impl->sendMessage((uint32_t)ClientMessageType::ADD_COMPONENT, &type, sizeof(type));
	}

	void EditorClient::toggleGameMode()
	{
		m_impl->sendMessage((uint32_t)ClientMessageType::TOGGLE_GAME_MODE, NULL, 0);
	}

	void EditorClient::addEntity()
	{
		m_impl->sendMessage((uint32_t)ClientMessageType::ADD_ENTITY, NULL, 0);
	}


	void EditorClient::mouseDown(int x, int y, int button)
	{
		int data[3] = {x, y, button};
		m_impl->sendMessage(ClientMessageType::POINTER_DOWN, data, 12);
	}


	void EditorClient::mouseUp(int x, int y, int button)
	{
		int data[3] = {x, y, button};
		m_impl->sendMessage(ClientMessageType::POINTER_UP, data, 12);
	}


	void EditorClient::mouseMove(int x, int y, int dx, int dy, int flags)
	{
		int data[] = {x, y, dx, dy, flags};
		m_impl->sendMessage(ClientMessageType::POINTER_MOVE, data, 20);

	}
	
	void EditorClient::loadUniverse(const char* path)
	{
		m_impl->m_universe_path = path;
		m_impl->sendMessage(ClientMessageType::LOAD, path, (int32_t)strlen(path)+1);
	}

	void EditorClient::setWireframe(bool is_wireframe)
	{
		int32_t data = is_wireframe;
		m_impl->sendMessage(ClientMessageType::SET_WIREFRAME, &data, sizeof(data));
	}

	void EditorClient::newUniverse()
	{
		m_impl->m_universe_path = "";
		m_impl->sendMessage(ClientMessageType::NEW_UNIVERSE, NULL, 0);
	}

	void EditorClient::setAnimableTime(int32_t time)
	{
		m_impl->sendMessage(ClientMessageType::SET_ANIMABLE_TIME, &time, sizeof(time));
	}

	void EditorClient::playPausePreviewAnimable()
	{
		m_impl->sendMessage(ClientMessageType::PLAY_PAUSE_ANIMABLE, NULL, 0);
	}

	void EditorClient::setEntityPosition(int32_t entity, const Vec3& position)
	{
		uint8_t data[sizeof(entity) + sizeof(position)];
		*(int32_t*)data = entity;
		*(Vec3*)(data + sizeof(entity)) = position;
		m_impl->sendMessage(ClientMessageType::SET_POSITION, data, sizeof(entity) + sizeof(position));
	}

	const char* EditorClient::getUniversePath() const
	{
		return m_impl->m_universe_path.c_str();
	}

	void EditorClient::saveUniverse(const char* path)
	{
		m_impl->m_universe_path = path;
		m_impl->sendMessage(ClientMessageType::SAVE, path, (int32_t)strlen(path)+1);
	}

	void EditorClient::navigate(float forward, float right, float speed)
	{
		uint8_t data[12];
		*(float*)data = forward;
		*(float*)(data + 4) = right;
		*(float*)(data + 8) = speed;
		m_impl->sendMessage(ClientMessageType::MOVE_CAMERA, data, 12);
	}


	void EditorClient::setComponentProperty(const char* component, const char* property, const void* value, int32_t length)
	{
		static Blob stream;
		stream.clearBuffer();
		uint32_t tmp = crc32(component);
		stream.write(&tmp, sizeof(tmp));
		tmp = crc32(property);
		stream.write(&tmp, sizeof(tmp));
		stream.write(&length, sizeof(length));
		stream.write(value, length);
		m_impl->sendMessage(ClientMessageType::PROPERTY_SET, stream.getBuffer(), stream.getBufferSize());
	}


	void EditorClient::requestProperties(uint32_t type_crc)
	{
		m_impl->sendMessage(ClientMessageType::GET_PROPERTIES, &type_crc, sizeof(type_crc));
	}


} // ~namespace Lumix
