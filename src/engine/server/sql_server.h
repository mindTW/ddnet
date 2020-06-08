#ifndef ENGINE_SERVER_SQL_SERVER_H
#define ENGINE_SERVER_SQL_SERVER_H

#include <base/tl/threading.h>

#include <mysql_connection.h>

#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/statement.h>

class CSqlServer
{
public:
	CSqlServer(const char *pDatabase, const char *pPrefix, const char *pUser, const char *pPass, const char *pIp, int Port, lock *pGlobalLock, bool ReadOnly = true, bool SetUpDb = false);
	~CSqlServer();

	bool Connect();
	void Disconnect();
	bool CreateTables();

	void executeSql(const char *pCommand);
	void executeSqlQuery(const char *pQuery);

	sql::ResultSet* GetResults() { return m_pResults; }

	const char* GetDatabase() { return m_aDatabase; }
	const char* GetPrefix() { return m_aPrefix; }
	const char* GetUser() { return m_aUser; }
	const char* GetPass() { return m_aPass; }
	const char* GetIP() { return m_aIp; }
	int GetPort() { return m_Port; }

	static int ms_NumReadServer;
	static int ms_NumWriteServer;

private:
	sql::Driver *m_pDriver;
	sql::Connection *m_pConnection;
	sql::Statement *m_pStatement;
	sql::ResultSet *m_pResults;

	// copy of config vars
	char m_aDatabase[64];
	char m_aPrefix[64];
	char m_aUser[64];
	char m_aPass[64];
	char m_aIp[64];
	int m_Port;

	bool m_SetUpDB;

	lock m_SqlLock;
	lock *m_pGlobalLock;
};

#endif
