#pragma once

#include "operation.h"

namespace csp
{
	class OpAlt : public Operation, ChannelAttachmentIn_i
	{
	public:
		OpAlt();
		virtual ~OpAlt();

		virtual WorkResult::Enum Evaluate( Host& host );
		virtual WorkResult::Enum Work( Host& host, time_t dt );

	private:
		virtual bool Init( lua::LuaStack& args, InitError& initError );
		bool CheckArgs( lua::LuaStack& args, InitError& initError ) const;
		void InitCases( lua::LuaStack& args );

		void UnrefChannels( lua::LuaStack const& stack );
		void UnrefArguments( lua::LuaStack const& stack );
		void UnrefClosures( lua::LuaStack const& stack );
		void DetachChannels() const;

		WorkResult::Enum StartTriggeredProcess( Host& host );
		void SelectProcessToTrigger( Host& host );

		virtual void MoveChannelArguments( Channel& channel, ChannelArgument* arguments, int numArguments );
		virtual Process& ProcessToEvaluate();

		struct AltCase
		{
			AltCase();

			lua::LuaRef_t m_closureRefKey;

			Channel* m_pChannel;
			lua::LuaRef_t m_channelRefKey;
			time_t m_time;				
		};
		AltCase* m_cases;
		int m_numCases;

		AltCase* m_pCaseTriggered;
		AltCase* m_pNilCase;

		Process m_process;
		lua::LuaRef_t m_processRefKey;

		ChannelArgument* m_arguments;
		int m_numArguments;
		bool m_argumentsMoved;
	};
}