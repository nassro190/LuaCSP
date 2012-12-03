
#include "process.h"
#include "operation.h"
#include "host.h"

csp::Process::Process()
	: m_luaThread()
	, m_parentProcess()
	, m_operation()
{
}

csp::Process::~Process()
{
	assert( m_operation == NULL );
}

lua::LuaState & csp::Process::LuaThread()
{
	return m_luaThread;
}

csp::Process* csp::Process::GetProcess( lua_State* luaState )
{
	void* pUserData = lua::LuaState::GetUserData( luaState );
	return static_cast< Process* >( pUserData );
}

void csp::Process::SetProcess( lua_State* luaState, Process* process )
{
	lua::LuaState::SetUserData( luaState, process );
}

void csp::Process::SwitchCurrentOperation( Operation* pOperation )
{
	if( m_operation )
	{
		delete m_operation;
		m_operation = NULL;
	}

	m_operation = pOperation;
}

csp::Operation& csp::Process::CurrentOperation()
{
	assert( m_operation );
	return *m_operation;
}

void csp::Process::Work( Host& host, time_t dt )
{
	assert( m_operation );
	WorkResult::Enum result = m_operation->Work( host, dt );
	
	if( result == WorkResult::FINISH )
	{
		m_operation->SetFinished( true );
		assert( m_luaThread.Status() == lua::Return::YIELD );
		host.PushEvalStep( *this );
	}
}


csp::WorkResult::Enum csp::Process::Evaluate( Host& host )
{
	int numArgs = 0;

	if( m_operation )
	{
		WorkResult::Enum result = m_operation->IsFinished()
			? WorkResult::FINISH
			: m_operation->Evaluate( host );

		if( result == WorkResult::FINISH )
		{
			lua::LuaStack luaStack = m_luaThread.GetStack();
			numArgs = m_operation->PushResults( luaStack );
			SwitchCurrentOperation( NULL );
		}
		else
			return WorkResult::YIELD;
	}
	
	WorkResult::Enum result = Resume( numArgs );
	
	if ( result == WorkResult::YIELD && m_operation )
		host.PushEvalStep( *this );
	else if ( result == WorkResult::FINISH && m_parentProcess )
		host.PushEvalStep( *m_parentProcess );		
	
	return result;
}


csp::WorkResult::Enum csp::Process::Resume( int numArgs )
{
	lua::Return::Enum retValue = LuaThread().Resume( numArgs, m_parentProcess ? &m_parentProcess->LuaThread() : NULL );
	if( retValue == lua::Return::YIELD )
		return WorkResult::YIELD;

	return WorkResult::FINISH;
}

bool csp::Process::IsRunning() const
{
	return m_operation != NULL || m_luaThread.InternalState() == NULL || m_luaThread.Status() == lua::Return::YIELD;
}

void csp::Process::SetLuaThread( const lua::LuaState& luaThread )
{
	m_luaThread = luaThread;
	Process::SetProcess( m_luaThread.InternalState(), this );
}

void csp::Process::SetParentProcess( Process& parentProcess )
{
	m_parentProcess = &parentProcess;
}