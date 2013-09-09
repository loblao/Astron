#include "util/Role.h"
#include "core/RoleFactory.h"
#include "core/global.h"
#include "core/messages.h"
#include <map>
#include "dcparser/dcClass.h"
#include <exception>
#include <stdexcept>

#include "DistributedObject.h"


std::map<unsigned int, DistributedObject*> distObjs;

ConfigVariable<channel_t> cfg_channel("control", 0);

class StateServer : public Role
{
public:
	StateServer(RoleConfig roleconfig) : Role(roleconfig)
	{
		channel_t channel = cfg_channel.get_rval(m_roleconfig);
		MessageDirector::singleton.subscribe_channel(this, channel);

		std::stringstream name;
		name << "StateServer(" << channel << ")";
		m_log = new LogCategory("stateserver", name.str());
	}

	void handle_generate(DatagramIterator &dgi, bool has_other)
	{
		unsigned int parent_id = dgi.read_uint32();
		unsigned int zone_id = dgi.read_uint32();
		unsigned short dc_id = dgi.read_uint16();
		unsigned int do_id = dgi.read_uint32();

		if(dc_id >= gDCF->get_num_classes())
		{
			m_log->error() << "Received create for unknown dclass ID=" << dc_id << std::endl;
			return;
		}

		if(distObjs.find(do_id) != distObjs.end())
		{
			m_log->warning() << "Received generate for already-existing object ID=" << do_id << std::endl;
			return;
		}

		DCClass *dclass = gDCF->get_class(dc_id);
		DistributedObject *obj;
		try
		{
			obj = new DistributedObject(do_id, dclass, parent_id, zone_id, dgi, has_other);
		}
		catch(std::exception &e)
		{
			m_log->error() << "Received truncated generate for "
			               << dclass->get_name() << "(" << do_id << ")" << std::endl;
			return;
		}
		distObjs[do_id] = obj;
	}

	virtual void handle_datagram(Datagram &in_dg, DatagramIterator &dgi)
	{
		channel_t sender = dgi.read_uint64();
		unsigned short msgtype = dgi.read_uint16();
		switch(msgtype)
		{
			case STATESERVER_OBJECT_GENERATE_WITH_REQUIRED:
			{
				handle_generate(dgi, false);
				break;
			}
			case STATESERVER_OBJECT_GENERATE_WITH_REQUIRED_OTHER:
			{
				handle_generate(dgi, true);
				break;
			}
			case STATESERVER_SHARD_RESET:
			{
				channel_t ai_channel = dgi.read_uint64();
				std::set <channel_t> targets;
				for(auto it = distObjs.begin(); it != distObjs.end(); ++it)
					if(it->second && it->second->m_ai_channel == ai_channel && it->second->m_ai_explicitly_set)
						targets.insert(it->second->m_do_id);

				if(targets.size())
				{
					Datagram dg(targets, sender, STATESERVER_SHARD_RESET);
					dg.add_uint64(ai_channel);
					send(dg);
				}
				break;
			}
			default:
				m_log->warning() << "Received unknown message: msgtype=" << msgtype << std::endl;
		}
	}

private:
	LogCategory *m_log;
};

RoleFactoryItem<StateServer> ss_fact("stateserver");
