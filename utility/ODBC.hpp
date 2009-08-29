#pragma once
#include <exception>
#include <string>
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <time.h>

#include <assert.h>
#include <vector>
#include <map>
#include <list>
#include <sstream>

#include <boost/utility.hpp>
#include <boost/type_traits.hpp>
#include <boost/static_assert.hpp>
#include <boost/any.hpp>
#include <algorithm>
/*
	re-implementation of ODBC layer using boost::any 
	and standard containers.
*/
/*
	Still have to solve the transaction issue.

	transactions get committed on the connection -ie calling
	'commit' causes ALL pending transactions to be committed.
	This is probably not a very big deal.
	Is it even worth trying to provide transaction support?
	I'm not using this to write servers, after all.

*/
namespace ODBC
{
	class exception : public std::exception
	{
	public:
		exception(std::string s="")
			:std::exception(s.c_str()){}
	};

	inline void CheckReturnValue(SQLSMALLINT HandleType,SQLHANDLE Handle,SQLRETURN ReturnValue)
	{
		if(SQL_SUCCESS == ReturnValue || SQL_SUCCESS_WITH_INFO == ReturnValue)
			return;		
		if(SQL_NO_DATA == ReturnValue)
			throw ODBC::exception("No data");

		if(SQL_ERROR != ReturnValue)
			throw ODBC::exception("error diagnosing error!");

		SQLCHAR       SqlState[6],  Msg[SQL_MAX_MESSAGE_LENGTH];
		SQLINTEGER    NativeError;
		SQLSMALLINT   MsgLen;
		int ret2=::SQLGetDiagRec(HandleType,Handle,1,SqlState,&NativeError,Msg,sizeof(Msg),&MsgLen);
		if(SQL_SUCCESS!=ret2)
			throw ODBC::exception("Error, but couldn't extract diagnostic data.");

		std::string s(Msg,Msg+MsgLen);
		throw ODBC::exception(s);		
	}



	typedef boost::any Field;

	struct CaseInsensitiveStringCompare : public std::binary_function <std::string, std::string, bool> 
	{
		bool operator()(std::string l,std::string r) const
		{
			std::transform (l.begin(),l.end(), l.begin(), tolower);
			std::transform (r.begin(),r.end(), r.begin(), tolower);
			return l < r ;

		}
	};

	typedef std::map<std::string,Field,CaseInsensitiveStringCompare> Row;
	
	class Environment:boost::noncopyable
	{
		SQLHENV EnvironmentHandle_;
	public:
		operator SQLHENV () const {	return EnvironmentHandle_;}


		void CheckSuccess(SQLRETURN ret)
		{
			CheckReturnValue(SQL_HANDLE_ENV,EnvironmentHandle_,ret);
		}
		Environment()
		{
			if(SQL_SUCCESS!=SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &EnvironmentHandle_))
				throw ODBC::exception("Couldn't allocate environment handle");
			SQLSetEnvAttr(EnvironmentHandle_, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0); 
		}
		~Environment()
		{
			SQLFreeHandle(SQL_HANDLE_ENV, EnvironmentHandle_);
			//if(SQL_SUCCESS!=SQLFreeHandle(SQL_HANDLE_ENV, EnvironmentHandle_))
			//	terminate();
		}
	};

	class Connection:boost::noncopyable
	{
		SQLHDBC ConnectionHandle_;
		Environment Environment_;

		void CheckSuccess(SQLRETURN ret)
		{
			CheckReturnValue(SQL_HANDLE_DBC,ConnectionHandle_,ret);
		}

	public:
		operator SQLHDBC()const	{return ConnectionHandle_;}

		Connection(std::string DSN, std::string User="", std::string Password="", LONG LoginTimeout=5)
		{

			if(SQL_SUCCESS!=SQLAllocHandle(SQL_HANDLE_DBC, Environment_, &ConnectionHandle_))
				throw ODBC::exception("Couldn't allocate connection handle");


			CheckSuccess(SQLSetConnectAttr(ConnectionHandle_, SQL_ATTR_LOGIN_TIMEOUT, (SQLPOINTER)&LoginTimeout, 0));
			//CheckSuccess(SQLSetConnectAttr(ConnectionHandle_,SQL_ATTR_ODBC_CURSORS,(SQLPOINTER)SQL_CUR_USE_ODBC,0));

			CheckSuccess(SQLSetConnectAttr(ConnectionHandle_,SQL_ATTR_AUTOCOMMIT,SQL_AUTOCOMMIT_OFF,0));
			
			CheckSuccess(
				SQLConnect(ConnectionHandle_, 
				(SQLCHAR*)DSN.c_str(), 		SQL_NTS,//I'm pretty certain these casts are safe.
				(SQLCHAR*)User.c_str(),		SQL_NTS,
				(SQLCHAR*)Password.c_str(), SQL_NTS)
				);
		}

		//!Commit any active transactions
		void Commit()
		{
			::SQLEndTran(SQL_HANDLE_DBC,ConnectionHandle_,SQL_COMMIT);//unfortunately this commits ALL statements associated with this connection!
		}

		~Connection()
		{
			////this should really be in a destructor!

			::SQLEndTran(SQL_HANDLE_DBC,ConnectionHandle_,SQL_ROLLBACK);//unfortunately this commits ALL statements associated with this connection!
			//
			SQLRETURN ret = SQLDisconnect(ConnectionHandle_);
			CheckSuccess(ret);
			if(SQL_SUCCESS!=ret)
				terminate();
			if(SQL_SUCCESS!=SQLFreeHandle(SQL_HANDLE_DBC, ConnectionHandle_))
				terminate();
		}
	};

	class Statement:boost::noncopyable
	{
		SQLHSTMT StatementHandle_;
	public:
		operator SQLHSTMT(){return StatementHandle_;}

		void CheckSuccess(SQLRETURN ret)
		{
			if(ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO /*|| ret == SQL_NO_DATA*/)
				return;

			CheckReturnValue(SQL_HANDLE_STMT,StatementHandle_,ret);
		}

		Statement(const Connection& connection)
		{
			
			CheckReturnValue(SQL_HANDLE_DBC,connection,
				SQLAllocHandle(SQL_HANDLE_STMT, connection, &StatementHandle_));

		}
		~Statement()
		{
			if(SQL_SUCCESS!=SQLFreeHandle(SQL_HANDLE_STMT, StatementHandle_))
				terminate();	
		}


		void Execute(std::string Query)
		{
			try
			{
				SQLRETURN ret = SQLExecDirect(StatementHandle_, (SQLCHAR*)Query.c_str(), SQL_NTS);
				if(SQL_NO_DATA!=ret)//this is ok
					CheckSuccess(ret);
			}
			catch(ODBC::exception& e)
			{
				std::ostringstream out;
				out << "Error executing SQL statement:" << std::endl
					<< Query << std::endl << e.what();
				throw ODBC::exception(out.str());
			}
		}
	};


	//followed by a specialisation for string.

	/*
		Probably actually want two classes: recordset and recordset_getter
	*/

	class Recordset : public std::list<Row>
	{

		//==============================================================================
		//From here to next marker all share 'statement', which implies a seperate class
		//==============================================================================
		
		template<typename T>
		Field ExtractValue2(Statement& statement,SQLSMALLINT type,int field)
		{
			T t;
			BOOST_STATIC_ASSERT((::boost::is_fundamental<T>::value));

			SQLINTEGER len_or_null_indicator;
			SQLRETURN ret = SQLGetData(statement,field+1,type,&t,sizeof(T),&len_or_null_indicator);
			statement.CheckSuccess(ret);

			if(SQL_NULL_DATA==len_or_null_indicator)
				return Field();//empty field
			return Field(t);
		}

		Field ExtractString(Statement& statement, int field)
		{
			std::string data;
			SQLINTEGER cbValue;
			data.clear();
			SQLRETURN ret;
			do
			{
				const int MAX_BUFFER=255;//where is this coming from???
				char temp[MAX_BUFFER];
				ret=SQLGetData(statement,(SQLUSMALLINT)field + 1, SQL_C_DEFAULT, temp, MAX_BUFFER, &cbValue);
				if(SQL_NULL_DATA==cbValue)//or more probably, return a NULL.
					if(data.empty())
						return Field();//ie a NULL
					else
						break;
				data.append(temp);

			}while(SQL_SUCCESS_WITH_INFO==ret);

			statement.CheckSuccess(ret);
			return data;
		}

		//!mge for smalldatetime Data Type in tables on SQL server. Aug 29, 2007
		TIMESTAMP_STRUCT ExtractDateTime(Statement& statement,int field)
		{
			TIMESTAMP_STRUCT tstamp;
			SQLINTEGER cbValue;
			SQLRETURN ret = SQLGetData(statement,field+1,/*93*/SQL_TYPE_TIMESTAMP,&tstamp,sizeof(TIMESTAMP_STRUCT),&cbValue);
			statement.CheckSuccess(ret);

			return tstamp;
		}

		Field ExtractValue(Statement& statement,int field, SQLSMALLINT type)
		{
			switch (type)
			{
			case SQL_CHAR:
				return ExtractValue2<SQLCHAR>(statement,type,field);
			case SQL_SMALLINT:
				return ExtractValue2<SQLSMALLINT>(statement,type,field);
			case SQL_INTEGER:
				return ExtractValue2<SQLINTEGER>(statement,type,field);
			case SQL_FLOAT:
				//return ExtractValue2<SQLFLOAT>(statement,type,field);
			case SQL_DOUBLE:
				return ExtractValue2<SQLDOUBLE>(statement,SQL_DOUBLE,field);
			case SQL_VARCHAR:
			case SQL_LONGVARCHAR:			
				return ExtractString(statement,field);
			case SQL_BIT:
				return ExtractValue2<BOOL>(statement,type,field);//Nov 15, 2005
			case SQL_TYPE_TIMESTAMP:
				return ExtractDateTime(statement,field);//Aug 29, 2007
			default:
				throw exception("unknown SQL data type");

			}
		}

		//================================
		//see above comment on 'statement'
		//================================

	public:

		//need to maintain column header information!
		struct Header
		{
			std::string name;
			SQLSMALLINT SQLType;
		};
		std::vector<Header> Headers;

		static Header GetFieldAttributes(Statement& statement,int field)
		{
			SQLCHAR FieldName[256];
	
			
			SQLSMALLINT cbColNameLen, SqlType, ibScale, fNullable;
			SQLUINTEGER ColSize;

			SQLRETURN ret=SQLDescribeCol(statement, field + 1, FieldName, 
				256, &cbColNameLen,
				&SqlType, &ColSize, &ibScale, &fNullable);

			statement.CheckSuccess(ret);
			Header h;
			h.name=std::string(FieldName,FieldName+cbColNameLen);
			h.SQLType=SqlType;
			return h;
		}


		Recordset(Connection& connection, std::string SQL)
		{
			Statement statement(connection);

			try
			{
				statement.CheckSuccess(
					SQLExecDirect(statement, (SQLCHAR*)SQL.c_str(), SQL_NTS));

				SQLRETURN ret = SQLFetch(statement);
			
				if(SQL_NO_DATA==ret)//I think that this is ok...
					return;

				statement.CheckSuccess(ret);
			
				SQLSMALLINT nFieldCount = 0;
				SQLNumResultCols(statement, &nFieldCount);

				for(int col=0;col<nFieldCount;col++)
					Headers.push_back(
						GetFieldAttributes(statement,col)
						);

				while(SQL_NO_DATA!=ret)
				{
					Row row;
					for (int field=0;field<nFieldCount;field++)
					{
						Header header=Headers[field];
						row[header.name]=ExtractValue(statement,field,header.SQLType);
					}
					push_back(row);
				
					ret=SQLFetchScroll(statement, SQL_FETCH_NEXT, 0);
				}
			}
			catch(ODBC::exception& e)
			{
				std::ostringstream out;
				out << "Error executing SQL statement:" << std::endl
					<< SQL << std::endl << e.what();
				throw ODBC::exception(out.str());
			}
		}; 
	};

	inline std::vector<std::string>
		GetDataSourceNames()
	{
		std::vector<std::string>names;
		
		Environment environment;
		
		SQLCHAR ServerName[SQL_MAX_DSN_LENGTH+1];
		/*SQLSMALLINT BufferLength2;*/
		SQLSMALLINT NameLength1,NameLength2;
		SQLCHAR Description[255];
		
		SQLRETURN ret=SQLDataSources(environment,SQL_FETCH_FIRST,ServerName,SQL_MAX_DSN_LENGTH+1,&NameLength1,Description,255,&NameLength2);

		environment.CheckSuccess(ret);

		while(ret==SQL_SUCCESS)
		{
			names.push_back(std::string(ServerName,ServerName+NameLength1));
			ret=SQLDataSources(environment,SQL_FETCH_NEXT,ServerName,SQL_MAX_DSN_LENGTH+1,&NameLength1,Description,255,&NameLength2);
		}

		return names;
	}

	template<typename T>
	struct ExtractValue
	{
		const std::string field_;
		ExtractValue(std::string field):field_(field){}
		T operator()(Row row)
		{
			return boost::any_cast<T>(row[field_]);
		}
	};

}//namespace ODBC