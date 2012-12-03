#include <luacpp/luacpp.h>

#include "process.h"
#include "operation.h"
#include "host.h"

csp::Operation::Operation()
	: m_pProcess()
	, m_finished( false )
{

}

csp::Operation::~Operation()
{
}

int csp::Operation::PushResults( lua::LuaStack & )
{
	return 0;
}

csp::Process& csp::Operation::ThisProcess() const
{
	assert( m_pProcess );
	return *m_pProcess;
}

int csp::Operation::Initialize( lua_State* luaState )
{
	lua::LuaState state( luaState );

	m_pProcess = csp::Process::GetProcess( luaState );
	if( m_pProcess == NULL )
	{
		delete this;
		return state.Error("not in CSP process");
	}

	lua::LuaStack args( luaState );
	if ( !Init( args ) )
	{
		delete this;
		return 0;
	}

	m_pProcess->SwitchCurrentOperation( this );
	return state.Yield( 0 );
}

bool csp::Operation::Init( lua::LuaStack & )
{
	return true;
}

bool csp::Operation::IsFinished() const
{
	return m_finished;
}

void csp::Operation::SetFinished( bool finished )
{
	m_finished = finished;
}

csp::WorkResult::Enum csp::Operation::Evaluate( Host& )
{
	return WorkResult::YIELD;
}


csp::OpSleep::OpSleep()
	: m_seconds()
{
}

bool csp::OpSleep::Init( lua::LuaStack & args )
{
	if ( !args[1].IsNumber() )
	{
		args[1].ArgError("seconds expected"); 
		return false;
	}

	m_seconds = args[1].GetNumber();
	SetFinished( m_seconds <= 0 );
	return true;
}

csp::WorkResult::Enum csp::OpSleep::Work( Host&, time_t dt )
{
	m_seconds -= dt;
	return m_seconds > 0 ? WorkResult::YIELD : WorkResult::FINISH;
}

namespace operations
{
	int sleep( lua_State* L );
	int par( lua_State* L );
}

namespace helpers
{
	int log( lua_State* L );
}

int helpers::log( lua_State* luaState )
{
	lua::LuaStack args( luaState );

	for( int i = 1; i <= args.NumArgs(); ++i )
	{
		lua::LuaStackValue arg = args[i];
		if( arg.IsNil() )
			lua::Print( "nil" );
		else if( arg.IsBoolean() )
			lua::Print( arg.GetBoolean() ? "true" : "false" );
		else if( arg.IsNumber() )
			lua::Print( "%.5f", arg.GetNumber() );
		else if( arg.IsString() )
			lua::Print( arg.GetString() );

		if( i < args.NumArgs() )
			lua::Print(" ");
	}

	return 0;
}

int operations::sleep( lua_State* luaState )
{
	csp::OpSleep* pSleep = new csp::OpSleep();
	return pSleep->Initialize( luaState );
}

int operations::par( lua_State* luaState )
{
	csp::OpPar* pPar = new csp::OpPar();
	return pPar->Initialize( luaState );
}

const csp::OperationDescription operationDescriptions[] =
{
	  helpers::log, "log"
	, operations::sleep, "sleep"
	, operations::par, "par"
	, NULL, NULL
};

void csp::RegisterOperations( lua::LuaState & state, lua::LuaStackValue & value, const OperationDescription descriptions[] )
{
	for( int i = 0; descriptions[i].function; ++i)
	{
		state.GetStack().PushCFunction( descriptions[i].function );
		state.SetField( value, descriptions[i].name );
	}
}

void csp::UnregisterOperations( lua::LuaState & state, lua::LuaStackValue & value, const OperationDescription descriptions[] )
{
	for( int i = 0; descriptions[i].function; ++i)
	{
		state.GetStack().PushNil();
		state.SetField( value, descriptions[i].name );
	}
}

void csp::RegisterStandardOperations( lua::LuaState & state, lua::LuaStackValue & value )
{
	RegisterOperations( state, value, operationDescriptions );
}

void csp::UnregisterStandardOperations( lua::LuaState & state, lua::LuaStackValue & value )
{
	UnregisterOperations( state, value, operationDescriptions );
}

csp::OpPar::OpPar()
	: m_closures()
	, m_numClosures()
	, m_closureToRun()
{
}

csp::OpPar::~OpPar()
{
	for( int i = 0; i < m_numClosures; ++i )
	{
		assert( m_closures[i].refKey == -1 );
	}

	delete[] m_closures;
	m_closures = NULL;
}

bool csp::OpPar::Init( lua::LuaStack& args )
{
	m_numClosures = 0;
	for( int i = 1; i <= args.NumArgs(); ++i )
	{
		lua::LuaStackValue arg = args[i];
		if ( arg.IsFunction() )
			++m_numClosures;
		else
		{
			arg.ArgError( "function closure expected" );
			return false;
		}
	}

	m_closures = new ParClosure[ m_numClosures ];

	for( int i = 1; i <= args.NumArgs(); ++i )
	{
		lua::LuaStackValue arg = args[i];
		ParClosure& closure = m_closures[ i-1 ];

		lua::LuaState thread = args.NewThread();
		arg.PushValue();
		args.XMove( thread.GetStack(), 1 );

		closure.process.SetLuaThread( thread );
		closure.process.SetParentProcess( ThisProcess() );
		closure.refKey = args.RefInRegistry();
	}

	return true;
}

csp::WorkResult::Enum csp::OpPar::Evaluate( Host& host )
{
	bool finished = true;

	if( m_closureToRun < m_numClosures )
	{
		ParClosure& closure = m_closures[ m_closureToRun++ ];

		if( m_closureToRun < m_numClosures )
		{
			host.PushEvalStep( ThisProcess() );
			finished = false;
		}

		closure.process.Evaluate( host );
	}

	if ( !CheckFinished() )
		finished = false;

	if (finished && !IsFinished())
		SetFinished( true );

	return IsFinished() ? WorkResult::FINISH : WorkResult::YIELD;
}

csp::WorkResult::Enum csp::OpPar::Work( Host& host, time_t dt )
{
	for( int i = 0; i < m_closureToRun; ++i )
	{
		Process& process = m_closures[ i ].process;
		if( process.IsRunning() )
			process.Work( host, dt );
	}

	return IsFinished() ? WorkResult::FINISH : WorkResult::YIELD;
}

bool csp::OpPar::CheckFinished()
{
	bool finished = true;
	for( int i = 0; i < m_closureToRun; ++i )
	{
		Process& process = m_closures[ i ].process;
		if( process.IsRunning() )
		{
			finished = false;
		}
		else if( m_closures[ i ].refKey != -1 )
		{
			ThisProcess().LuaThread().GetStack().UnrefInRegistry( m_closures[ i ].refKey );
			m_closures[ i ].refKey = -1;
		}
	}
	return finished;
}
