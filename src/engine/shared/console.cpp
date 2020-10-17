/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>

#include <base/color.h>
#include <base/math.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/shared/protocol.h>
#include <engine/storage.h>

#include "config.h"
#include "console.h"
#include "linereader.h"

// todo: rework this

const char *CConsole::CResult::GetString(unsigned Index)
{
	if(Index >= m_NumArgs)
		return "";
	return m_apArgs[Index];
}

int CConsole::CResult::GetInteger(unsigned Index)
{
	if(Index >= m_NumArgs)
		return 0;
	return str_toint(m_apArgs[Index]);
}

float CConsole::CResult::GetFloat(unsigned Index)
{
	if(Index >= m_NumArgs)
		return 0.0f;
	return str_tofloat(m_apArgs[Index]);
}

ColorHSLA CConsole::CResult::GetColor(unsigned Index, bool Light)
{
	ColorHSLA hsl = ColorHSLA(0, 0, 0);
	if(Index >= m_NumArgs)
		return hsl;

	const char *pStr = m_apArgs[Index];
	if(str_isallnum(pStr) || ((pStr[0] == '-' || pStr[0] == '+') && str_isallnum(pStr + 1))) // Teeworlds Color (Packed HSL)
	{
		hsl = ColorHSLA(str_toulong_base(pStr, 10), true);
		if(Light)
			hsl = hsl.UnclampLighting();
	}
	else if(*pStr == '$') // Hex RGB
	{
		ColorRGBA rgb = ColorRGBA(0, 0, 0, 1);
		int Len = str_length(pStr);
		if(Len == 4)
		{
			unsigned Num = str_toulong_base(pStr + 1, 16);
			rgb.r = (((Num >> 8) & 0x0F) + ((Num >> 4) & 0xF0)) / 255.0f;
			rgb.g = (((Num >> 4) & 0x0F) + ((Num >> 0) & 0xF0)) / 255.0f;
			rgb.b = (((Num >> 0) & 0x0F) + ((Num << 4) & 0xF0)) / 255.0f;
		}
		else if(Len == 7)
		{
			unsigned Num = str_toulong_base(pStr + 1, 16);
			rgb.r = ((Num >> 16) & 0xFF) / 255.0f;
			rgb.g = ((Num >> 8) & 0xFF) / 255.0f;
			rgb.b = ((Num >> 0) & 0xFF) / 255.0f;
		}
		else
		{
			return hsl;
		}

		hsl = color_cast<ColorHSLA>(rgb);
	}
	else if(!str_comp_nocase(pStr, "red"))
		hsl = ColorHSLA(0.0f / 6.0f, 1, .5f);
	else if(!str_comp_nocase(pStr, "yellow"))
		hsl = ColorHSLA(1.0f / 6.0f, 1, .5f);
	else if(!str_comp_nocase(pStr, "green"))
		hsl = ColorHSLA(2.0f / 6.0f, 1, .5f);
	else if(!str_comp_nocase(pStr, "cyan"))
		hsl = ColorHSLA(3.0f / 6.0f, 1, .5f);
	else if(!str_comp_nocase(pStr, "blue"))
		hsl = ColorHSLA(4.0f / 6.0f, 1, .5f);
	else if(!str_comp_nocase(pStr, "magenta"))
		hsl = ColorHSLA(5.0f / 6.0f, 1, .5f);
	else if(!str_comp_nocase(pStr, "white"))
		hsl = ColorHSLA(0, 0, 1);
	else if(!str_comp_nocase(pStr, "gray"))
		hsl = ColorHSLA(0, 0, .5f);
	else if(!str_comp_nocase(pStr, "black"))
		hsl = ColorHSLA(0, 0, 0);

	return hsl;
}

const IConsole::CCommandInfo *CConsole::CCommand::NextCommandInfo(int AccessLevel, int FlagMask) const
{
	const CCommand *pInfo = m_pNext;
	while(pInfo)
	{
		if(pInfo->m_Flags & FlagMask && pInfo->m_AccessLevel >= AccessLevel)
			break;
		pInfo = pInfo->m_pNext;
	}
	return pInfo;
}

const IConsole::CCommandInfo *CConsole::FirstCommandInfo(int AccessLevel, int FlagMask) const
{
	for(const CCommand *pCommand = m_pFirstCommand; pCommand; pCommand = pCommand->m_pNext)
	{
		if(pCommand->m_Flags & FlagMask && pCommand->GetAccessLevel() >= AccessLevel)
			return pCommand;
	}

	return 0;
}

// the maximum number of tokens occurs in a string of length CONSOLE_MAX_STR_LENGTH with tokens size 1 separated by single spaces

int CConsole::ParseStart(CResult *pResult, const char *pString, int Length)
{
	char *pStr;
	int Len = sizeof(pResult->m_aStringStorage);
	if(Length < Len)
		Len = Length;

	str_copy(pResult->m_aStringStorage, pString, Len);
	pStr = pResult->m_aStringStorage;

	// get command
	pStr = str_skip_whitespaces(pStr);
	pResult->m_pCommand = pStr;
	pStr = str_skip_to_whitespace(pStr);

	if(*pStr)
	{
		pStr[0] = 0;
		pStr++;
	}

	pResult->m_pArgsStart = pStr;
	return 0;
}

int CConsole::ParseArgs(CResult *pResult, const char *pFormat)
{
	char Command = *pFormat;
	char *pStr;
	int Optional = 0;
	int Error = 0;

	pResult->ResetVictim();

	pStr = pResult->m_pArgsStart;

	while(1)
	{
		if(!Command)
			break;

		if(Command == '?')
			Optional = 1;
		else
		{
			pStr = str_skip_whitespaces(pStr);

			if(!(*pStr)) // error, non optional command needs value
			{
				if(!Optional)
				{
					Error = 1;
					break;
				}

				while(Command)
				{
					if(Command == 'v')
					{
						pResult->SetVictim(CResult::VICTIM_ME);
						break;
					}
					Command = NextParam(pFormat);
				}
				break;
			}

			// add token
			if(*pStr == '"')
			{
				char *pDst;
				pStr++;
				pResult->AddArgument(pStr);

				pDst = pStr; // we might have to process escape data
				while(1)
				{
					if(pStr[0] == '"')
						break;
					else if(pStr[0] == '\\')
					{
						if(pStr[1] == '\\')
							pStr++; // skip due to escape
						else if(pStr[1] == '"')
							pStr++; // skip due to escape
					}
					else if(pStr[0] == 0)
						return 1; // return error

					*pDst = *pStr;
					pDst++;
					pStr++;
				}

				// write null termination
				*pDst = 0;

				pStr++;
			}
			else
			{
				char *pVictim = 0;

				pResult->AddArgument(pStr);
				if(Command == 'v')
				{
					pVictim = pStr;
				}

				if(Command == 'r') // rest of the string
					break;
				else if(Command == 'v') // validate victim
					pStr = str_skip_to_whitespace(pStr);
				else if(Command == 'i') // validate int
					pStr = str_skip_to_whitespace(pStr);
				else if(Command == 'f') // validate float
					pStr = str_skip_to_whitespace(pStr);
				else if(Command == 's') // validate string
					pStr = str_skip_to_whitespace(pStr);

				if(pStr[0] != 0) // check for end of string
				{
					pStr[0] = 0;
					pStr++;
				}

				if(pVictim)
				{
					pResult->SetVictim(pVictim);
				}
			}
		}
		// fetch next command
		Command = NextParam(pFormat);
	}

	return Error;
}

char CConsole::NextParam(const char *&pFormat)
{
	if(*pFormat)
	{
		pFormat++;

		if(*pFormat == '[')
		{
			// skip bracket contents
			for(; *pFormat != ']'; pFormat++)
			{
				if(!*pFormat)
					return *pFormat;
			}

			// skip ']'
			pFormat++;

			// skip space if there is one
			if(*pFormat == ' ')
				pFormat++;
		}
	}
	return *pFormat;
}

int CConsole::RegisterPrintCallback(int OutputLevel, FPrintCallback pfnPrintCallback, void *pUserData)
{
	if(m_NumPrintCB == MAX_PRINT_CB)
		return -1;

	m_aPrintCB[m_NumPrintCB].m_OutputLevel = clamp(OutputLevel, (int)(OUTPUT_LEVEL_STANDARD), (int)(OUTPUT_LEVEL_DEBUG));
	m_aPrintCB[m_NumPrintCB].m_pfnPrintCallback = pfnPrintCallback;
	m_aPrintCB[m_NumPrintCB].m_pPrintCallbackUserdata = pUserData;
	return m_NumPrintCB++;
}

void CConsole::SetPrintOutputLevel(int Index, int OutputLevel)
{
	if(Index >= 0 && Index < MAX_PRINT_CB)
		m_aPrintCB[Index].m_OutputLevel = clamp(OutputLevel, (int)(OUTPUT_LEVEL_STANDARD), (int)(OUTPUT_LEVEL_DEBUG));
}

char *CConsole::Format(char *pBuf, int Size, const char *pFrom, const char *pStr)
{
	char aTimeBuf[80];
	str_timestamp_format(aTimeBuf, sizeof(aTimeBuf), FORMAT_TIME);

	str_format(pBuf, Size, "[%s][%s]: %s", aTimeBuf, pFrom, pStr);
	return pBuf;
}

void CConsole::Print(int Level, const char *pFrom, const char *pStr, bool Highlighted)
{
	dbg_msg(pFrom, "%s", pStr);
	char aBuf[1024];
	Format(aBuf, sizeof(aBuf), pFrom, pStr);
	for(int i = 0; i < m_NumPrintCB; ++i)
	{
		if(Level <= m_aPrintCB[i].m_OutputLevel && m_aPrintCB[i].m_pfnPrintCallback)
		{
			m_aPrintCB[i].m_pfnPrintCallback(aBuf, m_aPrintCB[i].m_pPrintCallbackUserdata, Highlighted);
		}
	}
}

void CConsole::SetTeeHistorianCommandCallback(FTeeHistorianCommandCallback pfnCallback, void *pUser)
{
	m_pfnTeeHistorianCommandCallback = pfnCallback;
	m_pTeeHistorianCommandUserdata = pUser;
}

bool CConsole::LineIsValid(const char *pStr)
{
	if(!pStr || *pStr == 0)
		return false;

	do
	{
		CResult Result;
		const char *pEnd = pStr;
		const char *pNextPart = 0;
		int InString = 0;

		while(*pEnd)
		{
			if(*pEnd == '"')
				InString ^= 1;
			else if(*pEnd == '\\') // escape sequences
			{
				if(pEnd[1] == '"')
					pEnd++;
			}
			else if(!InString)
			{
				if(*pEnd == ';') // command separator
				{
					pNextPart = pEnd + 1;
					break;
				}
				else if(*pEnd == '#') // comment, no need to do anything more
					break;
			}

			pEnd++;
		}

		if(ParseStart(&Result, pStr, (pEnd - pStr) + 1) != 0)
			return false;

		CCommand *pCommand = FindCommand(Result.m_pCommand, m_FlagMask);
		if(!pCommand || ParseArgs(&Result, pCommand->m_pParams))
			return false;

		pStr = pNextPart;
	} while(pStr && *pStr);

	return true;
}

void CConsole::ExecuteLineStroked(int Stroke, const char *pStr, int ClientID, bool InterpretSemicolons)
{
	const char *pWithoutPrefix = str_startswith(pStr, "mc;");
	if(pWithoutPrefix)
	{
		InterpretSemicolons = true;
		pStr = pWithoutPrefix;
	}
	while(pStr && *pStr)
	{
		CResult Result;
		Result.m_ClientID = ClientID;
		const char *pEnd = pStr;
		const char *pNextPart = 0;
		int InString = 0;

		while(*pEnd)
		{
			if(*pEnd == '"')
				InString ^= 1;
			else if(*pEnd == '\\') // escape sequences
			{
				if(pEnd[1] == '"')
					pEnd++;
			}
			else if(!InString && InterpretSemicolons)
			{
				if(*pEnd == ';') // command separator
				{
					pNextPart = pEnd + 1;
					break;
				}
				else if(*pEnd == '#') // comment, no need to do anything more
					break;
			}

			pEnd++;
		}

		if(ParseStart(&Result, pStr, (pEnd - pStr) + 1) != 0)
			return;

		if(!*Result.m_pCommand)
			return;

		CCommand *pCommand = FindCommand(Result.m_pCommand, m_FlagMask);

		if(pCommand)
		{
			if(ClientID == IConsole::CLIENT_ID_GAME && !(pCommand->m_Flags & CFGFLAG_GAME))
			{
				if(Stroke)
				{
					char aBuf[96];
					str_format(aBuf, sizeof(aBuf), "Command '%s' cannot be executed from a map.", Result.m_pCommand);
					Print(OUTPUT_LEVEL_STANDARD, "console", aBuf);
				}
			}
			else if(ClientID == IConsole::CLIENT_ID_NO_GAME && pCommand->m_Flags & CFGFLAG_GAME)
			{
				if(Stroke)
				{
					char aBuf[96];
					str_format(aBuf, sizeof(aBuf), "Command '%s' cannot be executed from a non-map config file.", Result.m_pCommand);
					Print(OUTPUT_LEVEL_STANDARD, "console", aBuf);
					str_format(aBuf, sizeof(aBuf), "Hint: Put the command in '%s.cfg' instead of '%s.map.cfg' ", g_Config.m_SvMap, g_Config.m_SvMap);
					Print(OUTPUT_LEVEL_STANDARD, "console", aBuf);
				}
			}
			else if(pCommand->GetAccessLevel() >= m_AccessLevel)
			{
				int IsStrokeCommand = 0;
				if(Result.m_pCommand[0] == '+')
				{
					// insert the stroke direction token
					Result.AddArgument(m_paStrokeStr[Stroke]);
					IsStrokeCommand = 1;
				}

				if(Stroke || IsStrokeCommand)
				{
					if(ParseArgs(&Result, pCommand->m_pParams))
					{
						char aBuf[256];
						str_format(aBuf, sizeof(aBuf), "Invalid arguments... Usage: %s %s", pCommand->m_pName, pCommand->m_pParams);
						Print(OUTPUT_LEVEL_STANDARD, "console", aBuf);
					}
					else if(m_StoreCommands && pCommand->m_Flags & CFGFLAG_STORE)
					{
						m_ExecutionQueue.AddEntry();
						m_ExecutionQueue.m_pLast->m_pfnCommandCallback = pCommand->m_pfnCallback;
						m_ExecutionQueue.m_pLast->m_pCommandUserData = pCommand->m_pUserData;
						m_ExecutionQueue.m_pLast->m_Result = Result;
					}
					else
					{
						if(pCommand->m_Flags & CMDFLAG_TEST && !g_Config.m_SvTestingCommands)
							return;

						if(m_pfnTeeHistorianCommandCallback && !(pCommand->m_Flags & CFGFLAG_NONTEEHISTORIC))
						{
							m_pfnTeeHistorianCommandCallback(ClientID, m_FlagMask, pCommand->m_pName, &Result, m_pTeeHistorianCommandUserdata);
						}

						if(Result.GetVictim() == CResult::VICTIM_ME)
							Result.SetVictim(ClientID);

						if(Result.HasVictim() && Result.GetVictim() == CResult::VICTIM_ALL)
						{
							for(int i = 0; i < MAX_CLIENTS; i++)
							{
								Result.SetVictim(i);
								pCommand->m_pfnCallback(&Result, pCommand->m_pUserData);
							}
						}
						else
						{
							pCommand->m_pfnCallback(&Result, pCommand->m_pUserData);
						}

						if(pCommand->m_Flags & CMDFLAG_TEST)
							m_Cheated = true;
					}
				}
			}
			else if(Stroke)
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "Access for command %s denied.", Result.m_pCommand);
				Print(OUTPUT_LEVEL_STANDARD, "console", aBuf);
			}
		}
		else if(Stroke)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "No such command: %s.", Result.m_pCommand);
			Print(OUTPUT_LEVEL_STANDARD, "console", aBuf);
		}

		pStr = pNextPart;
	}
}

void CConsole::PossibleCommands(const char *pStr, int FlagMask, bool Temp, FPossibleCallback pfnCallback, void *pUser)
{
	for(CCommand *pCommand = m_pFirstCommand; pCommand; pCommand = pCommand->m_pNext)
	{
		if(pCommand->m_Flags & FlagMask && pCommand->m_Temp == Temp)
		{
			if(str_find_nocase(pCommand->m_pName, pStr))
				pfnCallback(pCommand->m_pName, pUser);
		}
	}
}

CConsole::CCommand *CConsole::FindCommand(const char *pName, int FlagMask)
{
	for(CCommand *pCommand = m_pFirstCommand; pCommand; pCommand = pCommand->m_pNext)
	{
		if(pCommand->m_Flags & FlagMask)
		{
			if(str_comp_nocase(pCommand->m_pName, pName) == 0)
				return pCommand;
		}
	}

	return 0x0;
}

void CConsole::ExecuteLine(const char *pStr, int ClientID, bool InterpretSemicolons)
{
	CConsole::ExecuteLineStroked(1, pStr, ClientID, InterpretSemicolons); // press it
	CConsole::ExecuteLineStroked(0, pStr, ClientID, InterpretSemicolons); // then release it
}

void CConsole::ExecuteLineFlag(const char *pStr, int FlagMask, int ClientID, bool InterpretSemicolons)
{
	int Temp = m_FlagMask;
	m_FlagMask = FlagMask;
	ExecuteLine(pStr, ClientID, InterpretSemicolons);
	m_FlagMask = Temp;
}

void CConsole::ExecuteFile(const char *pFilename, int ClientID, bool LogFailure, int StorageType)
{
	// make sure that this isn't being executed already
	for(CExecFile *pCur = m_pFirstExec; pCur; pCur = pCur->m_pPrev)
		if(str_comp(pFilename, pCur->m_pFilename) == 0)
			return;

	if(!m_pStorage)
		m_pStorage = Kernel()->RequestInterface<IStorage>();
	if(!m_pStorage)
		return;

	// push this one to the stack
	CExecFile ThisFile;
	CExecFile *pPrev = m_pFirstExec;
	ThisFile.m_pFilename = pFilename;
	ThisFile.m_pPrev = m_pFirstExec;
	m_pFirstExec = &ThisFile;

	// exec the file
	IOHANDLE File = m_pStorage->OpenFile(pFilename, IOFLAG_READ, StorageType);

	char aBuf[128];
	if(File)
	{
		char *pLine;
		CLineReader lr;

		str_format(aBuf, sizeof(aBuf), "executing '%s'", pFilename);
		Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", aBuf);
		lr.Init(File);

		while((pLine = lr.Get()))
			ExecuteLine(pLine, ClientID);

		io_close(File);
	}
	else if(LogFailure)
	{
		str_format(aBuf, sizeof(aBuf), "failed to open '%s'", pFilename);
		Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", aBuf);
	}

	m_pFirstExec = pPrev;
}

void CConsole::Con_Echo(IResult *pResult, void *pUserData)
{
	((CConsole *)pUserData)->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", pResult->GetString(0));
}

void CConsole::Con_Exec(IResult *pResult, void *pUserData)
{
	((CConsole *)pUserData)->ExecuteFile(pResult->GetString(0), -1, true, IStorage::TYPE_ALL);
}

void CConsole::ConCommandAccess(IResult *pResult, void *pUser)
{
	CConsole *pConsole = static_cast<CConsole *>(pUser);
	char aBuf[128];
	CCommand *pCommand = pConsole->FindCommand(pResult->GetString(0), CFGFLAG_SERVER);
	if(pCommand)
	{
		if(pResult->NumArguments() == 2)
		{
			pCommand->SetAccessLevel(pResult->GetInteger(1));
			str_format(aBuf, sizeof(aBuf), "moderator access for '%s' is now %s", pResult->GetString(0), pCommand->GetAccessLevel() ? "enabled" : "disabled");
			pConsole->Print(OUTPUT_LEVEL_STANDARD, "console", aBuf);
			str_format(aBuf, sizeof(aBuf), "helper access for '%s' is now %s", pResult->GetString(0), pCommand->GetAccessLevel() >= ACCESS_LEVEL_HELPER ? "enabled" : "disabled");
			pConsole->Print(OUTPUT_LEVEL_STANDARD, "console", aBuf);
			str_format(aBuf, sizeof(aBuf), "user access for '%s' is now %s", pResult->GetString(0), pCommand->GetAccessLevel() >= ACCESS_LEVEL_USER ? "enabled" : "disabled");
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "moderator access for '%s' is %s", pResult->GetString(0), pCommand->GetAccessLevel() ? "enabled" : "disabled");
			pConsole->Print(OUTPUT_LEVEL_STANDARD, "console", aBuf);
			str_format(aBuf, sizeof(aBuf), "helper access for '%s' is %s", pResult->GetString(0), pCommand->GetAccessLevel() >= ACCESS_LEVEL_HELPER ? "enabled" : "disabled");
			pConsole->Print(OUTPUT_LEVEL_STANDARD, "console", aBuf);
			str_format(aBuf, sizeof(aBuf), "user access for '%s' is %s", pResult->GetString(0), pCommand->GetAccessLevel() >= ACCESS_LEVEL_USER ? "enabled" : "disabled");
		}
	}
	else
		str_format(aBuf, sizeof(aBuf), "No such command: '%s'.", pResult->GetString(0));

	pConsole->Print(OUTPUT_LEVEL_STANDARD, "console", aBuf);
}

void CConsole::ConCommandStatus(IResult *pResult, void *pUser)
{
	CConsole *pConsole = static_cast<CConsole *>(pUser);
	char aBuf[240];
	mem_zero(aBuf, sizeof(aBuf));
	int Used = 0;

	for(CCommand *pCommand = pConsole->m_pFirstCommand; pCommand; pCommand = pCommand->m_pNext)
	{
		if(pCommand->m_Flags & pConsole->m_FlagMask && pCommand->GetAccessLevel() >= clamp(pResult->GetInteger(0), (int)ACCESS_LEVEL_ADMIN, (int)ACCESS_LEVEL_USER))
		{
			int Length = str_length(pCommand->m_pName);
			if(Used + Length + 2 < (int)(sizeof(aBuf)))
			{
				if(Used > 0)
				{
					Used += 2;
					str_append(aBuf, ", ", sizeof(aBuf));
				}
				str_append(aBuf, pCommand->m_pName, sizeof(aBuf));
				Used += Length;
			}
			else
			{
				pConsole->Print(OUTPUT_LEVEL_STANDARD, "console", aBuf);
				mem_zero(aBuf, sizeof(aBuf));
				str_copy(aBuf, pCommand->m_pName, sizeof(aBuf));
				Used = Length;
			}
		}
	}
	if(Used > 0)
		pConsole->Print(OUTPUT_LEVEL_STANDARD, "console", aBuf);
}

void CConsole::ConUserCommandStatus(IResult *pResult, void *pUser)
{
	CConsole *pConsole = static_cast<CConsole *>(pUser);
	CResult Result;
	Result.m_pCommand = "access_status";
	char aBuf[4];
	str_format(aBuf, sizeof(aBuf), "%d", IConsole::ACCESS_LEVEL_USER);
	Result.AddArgument(aBuf);

	pConsole->ConCommandStatus(&Result, pConsole);
}

struct CIntVariableData
{
	IConsole *m_pConsole;
	int *m_pVariable;
	int m_Min;
	int m_Max;
	int m_OldValue;
};

struct CColVariableData
{
	IConsole *m_pConsole;
	unsigned *m_pVariable;
	bool m_Light;
	bool m_Alpha;
	unsigned m_OldValue;
};

struct CStrVariableData
{
	IConsole *m_pConsole;
	char *m_pStr;
	int m_MaxSize;
	char *m_pOldValue;
};

static void IntVariableCommand(IConsole::IResult *pResult, void *pUserData)
{
	CIntVariableData *pData = (CIntVariableData *)pUserData;

	if(pResult->NumArguments())
	{
		int Val = pResult->GetInteger(0);

		// do clamping
		if(pData->m_Min != pData->m_Max)
		{
			if(Val < pData->m_Min)
				Val = pData->m_Min;
			if(pData->m_Max != 0 && Val > pData->m_Max)
				Val = pData->m_Max;
		}

		*(pData->m_pVariable) = Val;
		if(pResult->m_ClientID != IConsole::CLIENT_ID_GAME)
			pData->m_OldValue = Val;
	}
	else
	{
		char aBuf[32];
		str_format(aBuf, sizeof(aBuf), "Value: %d", *(pData->m_pVariable));
		pData->m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", aBuf);
	}
}

static void ColVariableCommand(IConsole::IResult *pResult, void *pUserData)
{
	CColVariableData *pData = (CColVariableData *)pUserData;

	if(pResult->NumArguments())
	{
		ColorHSLA Col = pResult->GetColor(0, pData->m_Light);
		int Val = Col.Pack(pData->m_Light ? 0.5f : 0.0f, pData->m_Alpha);

		*(pData->m_pVariable) = Val;
		if(pResult->m_ClientID != IConsole::CLIENT_ID_GAME)
			pData->m_OldValue = Val;
	}
	else
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Value: %u", *(pData->m_pVariable));
		pData->m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", aBuf);

		ColorHSLA hsl(*(pData->m_pVariable), true);
		if(pData->m_Light)
			hsl = hsl.UnclampLighting();
		str_format(aBuf, sizeof(aBuf), "H: %d°, S: %d%%, L: %d%%", round_truncate(hsl.h * 360), round_truncate(hsl.s * 100), round_truncate(hsl.l * 100));
		pData->m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", aBuf);

		ColorRGBA rgb = color_cast<ColorRGBA>(hsl);
		str_format(aBuf, sizeof(aBuf), "R: %d, G: %d, B: %d, #%06X", round_truncate(rgb.r * 255), round_truncate(rgb.g * 255), round_truncate(rgb.b * 255), rgb.Pack(false));
		pData->m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", aBuf);

		if(pData->m_Alpha)
		{
			str_format(aBuf, sizeof(aBuf), "A: %d%%", round_truncate(hsl.a * 100));
			pData->m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", aBuf);
		}
	}
}

static void StrVariableCommand(IConsole::IResult *pResult, void *pUserData)
{
	CStrVariableData *pData = (CStrVariableData *)pUserData;

	if(pResult->NumArguments())
	{
		const char *pString = pResult->GetString(0);
		if(!str_utf8_check(pString))
		{
			char aTemp[4];
			int Length = 0;
			while(*pString)
			{
				int Size = str_utf8_encode(aTemp, static_cast<unsigned char>(*pString++));
				if(Length + Size < pData->m_MaxSize)
				{
					mem_copy(pData->m_pStr + Length, aTemp, Size);
					Length += Size;
				}
				else
					break;
			}
			pData->m_pStr[Length] = 0;
		}
		else
			str_copy(pData->m_pStr, pString, pData->m_MaxSize);

		if(pResult->m_ClientID != IConsole::CLIENT_ID_GAME)
			str_copy(pData->m_pOldValue, pData->m_pStr, pData->m_MaxSize);
	}
	else
	{
		char aBuf[1024];
		str_format(aBuf, sizeof(aBuf), "Value: %s", pData->m_pStr);
		pData->m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", aBuf);
	}
}

void CConsole::ConToggle(IConsole::IResult *pResult, void *pUser)
{
	CConsole *pConsole = static_cast<CConsole *>(pUser);
	char aBuf[128] = {0};
	CCommand *pCommand = pConsole->FindCommand(pResult->GetString(0), pConsole->m_FlagMask);
	if(pCommand)
	{
		FCommandCallback pfnCallback = pCommand->m_pfnCallback;
		void *pUserData = pCommand->m_pUserData;

		// check for chain
		if(pCommand->m_pfnCallback == Con_Chain)
		{
			CChain *pChainInfo = static_cast<CChain *>(pCommand->m_pUserData);
			pfnCallback = pChainInfo->m_pfnCallback;
			pUserData = pChainInfo->m_pCallbackUserData;
		}

		if(pfnCallback == IntVariableCommand)
		{
			CIntVariableData *pData = static_cast<CIntVariableData *>(pUserData);
			int Val = *(pData->m_pVariable) == pResult->GetInteger(1) ? pResult->GetInteger(2) : pResult->GetInteger(1);
			str_format(aBuf, sizeof(aBuf), "%s %i", pResult->GetString(0), Val);
			pConsole->ExecuteLine(aBuf);
			aBuf[0] = 0;
		}
		else if(pfnCallback == StrVariableCommand)
		{
			CStrVariableData *pData = static_cast<CStrVariableData *>(pUserData);
			const char *pStr = !str_comp(pData->m_pStr, pResult->GetString(1)) ? pResult->GetString(2) : pResult->GetString(1);
			str_format(aBuf, sizeof(aBuf), "%s \"", pResult->GetString(0));
			char *pDst = aBuf + str_length(aBuf);
			str_escape(&pDst, pStr, aBuf + sizeof(aBuf));
			str_append(aBuf, "\"", sizeof(aBuf));
			pConsole->ExecuteLine(aBuf);
			aBuf[0] = 0;
		}
		else if(pfnCallback == ColVariableCommand)
		{
			CColVariableData *pData = static_cast<CColVariableData *>(pUserData);
			bool Light = pData->m_Light;
			float Darkest = Light ? 0.5f : 0.0f;
			bool Alpha = pData->m_Alpha;
			unsigned Cur = *pData->m_pVariable;
			ColorHSLA Val = Cur == pResult->GetColor(1, Light).Pack(Darkest, Alpha) ? pResult->GetColor(2, Light) : pResult->GetColor(1, Light);

			str_format(aBuf, sizeof(aBuf), "%s %u", pResult->GetString(0), Val.Pack(Darkest, Alpha));
			pConsole->ExecuteLine(aBuf);
			aBuf[0] = 0;
		}
		else
			str_format(aBuf, sizeof(aBuf), "Invalid command: '%s'.", pResult->GetString(0));
	}
	else
		str_format(aBuf, sizeof(aBuf), "No such command: '%s'.", pResult->GetString(0));

	if(aBuf[0] != 0)
		pConsole->Print(OUTPUT_LEVEL_STANDARD, "console", aBuf);
}

void CConsole::ConToggleStroke(IConsole::IResult *pResult, void *pUser)
{
	CConsole *pConsole = static_cast<CConsole *>(pUser);
	char aBuf[128] = {0};
	CCommand *pCommand = pConsole->FindCommand(pResult->GetString(1), pConsole->m_FlagMask);
	if(pCommand)
	{
		FCommandCallback pfnCallback = pCommand->m_pfnCallback;

		// check for chain
		if(pCommand->m_pfnCallback == Con_Chain)
		{
			CChain *pChainInfo = static_cast<CChain *>(pCommand->m_pUserData);
			pfnCallback = pChainInfo->m_pfnCallback;
		}

		if(pfnCallback == IntVariableCommand)
		{
			int Val = pResult->GetInteger(0) == 0 ? pResult->GetInteger(3) : pResult->GetInteger(2);
			str_format(aBuf, sizeof(aBuf), "%s %i", pResult->GetString(1), Val);
			pConsole->ExecuteLine(aBuf);
			aBuf[0] = 0;
		}
		else
			str_format(aBuf, sizeof(aBuf), "Invalid command: '%s'.", pResult->GetString(1));
	}
	else
		str_format(aBuf, sizeof(aBuf), "No such command: '%s'.", pResult->GetString(1));

	if(aBuf[0] != 0)
		pConsole->Print(OUTPUT_LEVEL_STANDARD, "console", aBuf);
}

CConsole::CConsole(int FlagMask)
{
	m_FlagMask = FlagMask;
	m_AccessLevel = ACCESS_LEVEL_ADMIN;
	m_pRecycleList = 0;
	m_TempCommands.Reset();
	m_StoreCommands = true;
	m_paStrokeStr[0] = "0";
	m_paStrokeStr[1] = "1";
	m_ExecutionQueue.Reset();
	m_pFirstCommand = 0;
	m_pFirstExec = 0;
	mem_zero(m_aPrintCB, sizeof(m_aPrintCB));
	m_NumPrintCB = 0;
	m_pfnTeeHistorianCommandCallback = 0;
	m_pTeeHistorianCommandUserdata = 0;

	m_pStorage = 0;

	// register some basic commands
	Register("echo", "r[text]", CFGFLAG_SERVER, Con_Echo, this, "Echo the text");
	Register("exec", "r[file]", CFGFLAG_SERVER | CFGFLAG_CLIENT, Con_Exec, this, "Execute the specified file");

	Register("toggle", "s[config-option] i[value 1] i[value 2]", CFGFLAG_SERVER | CFGFLAG_CLIENT, ConToggle, this, "Toggle config value");
	Register("+toggle", "s[config-option] i[value 1] i[value 2]", CFGFLAG_CLIENT, ConToggleStroke, this, "Toggle config value via keypress");

	Register("access_level", "s[command] ?i[accesslevel]", CFGFLAG_SERVER, ConCommandAccess, this, "Specify command accessibility (admin = 0, moderator = 1, helper = 2, all = 3)");
	Register("access_status", "i[accesslevel]", CFGFLAG_SERVER, ConCommandStatus, this, "List all commands which are accessible for admin = 0, moderator = 1, helper = 2, all = 3");
	Register("cmdlist", "", CFGFLAG_SERVER | CFGFLAG_CHAT, ConUserCommandStatus, this, "List all commands which are accessible for users");

// TODO: this should disappear
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Flags, Desc) \
	{ \
		static CIntVariableData Data = {this, &g_Config.m_##Name, Min, Max, Def}; \
		Register(#ScriptName, "?i", Flags, IntVariableCommand, &Data, Desc); \
	}

#define MACRO_CONFIG_COL(Name, ScriptName, Def, Flags, Desc) \
	{ \
		static CColVariableData Data = {this, &g_Config.m_##Name, static_cast<bool>((Flags)&CFGFLAG_COLLIGHT), \
			static_cast<bool>((Flags)&CFGFLAG_COLALPHA), Def}; \
		Register(#ScriptName, "?i", Flags, ColVariableCommand, &Data, Desc); \
	}

#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Flags, Desc) \
	{ \
		static char OldValue[Len] = Def; \
		static CStrVariableData Data = {this, g_Config.m_##Name, Len, OldValue}; \
		Register(#ScriptName, "?r", Flags, StrVariableCommand, &Data, Desc); \
	}

#include "config_variables.h"

#undef MACRO_CONFIG_INT
#undef MACRO_CONFIG_COL
#undef MACRO_CONFIG_STR

	// DDRace

	m_Cheated = false;
}

CConsole::~CConsole()
{
	CCommand *pCommand = m_pFirstCommand;
	while(pCommand)
	{
		CCommand *pNext = pCommand->m_pNext;
		if(pCommand->m_pfnCallback == Con_Chain)
			delete static_cast<CChain *>(pCommand->m_pUserData);
		// Temp commands are on m_TempCommands heap, so don't delete them
		if(!pCommand->m_Temp)
			delete pCommand;
		pCommand = pNext;
	}
}

void CConsole::ParseArguments(int NumArgs, const char **ppArguments)
{
	for(int i = 0; i < NumArgs; i++)
	{
		// check for scripts to execute
		if(ppArguments[i][0] == '-' && ppArguments[i][1] == 'f' && ppArguments[i][2] == 0)
		{
			if(NumArgs - i > 1)
				ExecuteFile(ppArguments[i + 1], -1, true, IStorage::TYPE_ABSOLUTE);
			i++;
		}
		else if(!str_comp("-s", ppArguments[i]) || !str_comp("--silent", ppArguments[i]))
		{
			// skip silent param
			continue;
		}
		else
		{
			// search arguments for overrides
			ExecuteLine(ppArguments[i]);
		}
	}
}

void CConsole::AddCommandSorted(CCommand *pCommand)
{
	if(!m_pFirstCommand || str_comp(pCommand->m_pName, m_pFirstCommand->m_pName) <= 0)
	{
		if(m_pFirstCommand && m_pFirstCommand->m_pNext)
			pCommand->m_pNext = m_pFirstCommand;
		else
			pCommand->m_pNext = 0;
		m_pFirstCommand = pCommand;
	}
	else
	{
		for(CCommand *p = m_pFirstCommand; p; p = p->m_pNext)
		{
			if(!p->m_pNext || str_comp(pCommand->m_pName, p->m_pNext->m_pName) <= 0)
			{
				pCommand->m_pNext = p->m_pNext;
				p->m_pNext = pCommand;
				break;
			}
		}
	}
}

void CConsole::Register(const char *pName, const char *pParams,
	int Flags, FCommandCallback pfnFunc, void *pUser, const char *pHelp)
{
	CCommand *pCommand = FindCommand(pName, Flags);
	bool DoAdd = false;
	if(pCommand == 0)
	{
		pCommand = new CCommand();
		DoAdd = true;
	}
	pCommand->m_pfnCallback = pfnFunc;
	pCommand->m_pUserData = pUser;

	pCommand->m_pName = pName;
	pCommand->m_pHelp = pHelp;
	pCommand->m_pParams = pParams;

	pCommand->m_Flags = Flags;
	pCommand->m_Temp = false;

	if(DoAdd)
		AddCommandSorted(pCommand);

	if(pCommand->m_Flags & CFGFLAG_CHAT)
		pCommand->SetAccessLevel(ACCESS_LEVEL_USER);
}

void CConsole::RegisterTemp(const char *pName, const char *pParams, int Flags, const char *pHelp)
{
	CCommand *pCommand;
	if(m_pRecycleList)
	{
		pCommand = m_pRecycleList;
		str_copy(const_cast<char *>(pCommand->m_pName), pName, TEMPCMD_NAME_LENGTH);
		str_copy(const_cast<char *>(pCommand->m_pHelp), pHelp, TEMPCMD_HELP_LENGTH);
		str_copy(const_cast<char *>(pCommand->m_pParams), pParams, TEMPCMD_PARAMS_LENGTH);

		m_pRecycleList = m_pRecycleList->m_pNext;
	}
	else
	{
		pCommand = new(m_TempCommands.Allocate(sizeof(CCommand))) CCommand;
		char *pMem = static_cast<char *>(m_TempCommands.Allocate(TEMPCMD_NAME_LENGTH));
		str_copy(pMem, pName, TEMPCMD_NAME_LENGTH);
		pCommand->m_pName = pMem;
		pMem = static_cast<char *>(m_TempCommands.Allocate(TEMPCMD_HELP_LENGTH));
		str_copy(pMem, pHelp, TEMPCMD_HELP_LENGTH);
		pCommand->m_pHelp = pMem;
		pMem = static_cast<char *>(m_TempCommands.Allocate(TEMPCMD_PARAMS_LENGTH));
		str_copy(pMem, pParams, TEMPCMD_PARAMS_LENGTH);
		pCommand->m_pParams = pMem;
	}

	pCommand->m_pfnCallback = 0;
	pCommand->m_pUserData = 0;
	pCommand->m_Flags = Flags;
	pCommand->m_Temp = true;

	AddCommandSorted(pCommand);
}

void CConsole::DeregisterTemp(const char *pName)
{
	if(!m_pFirstCommand)
		return;

	CCommand *pRemoved = 0;

	// remove temp entry from command list
	if(m_pFirstCommand->m_Temp && str_comp(m_pFirstCommand->m_pName, pName) == 0)
	{
		pRemoved = m_pFirstCommand;
		m_pFirstCommand = m_pFirstCommand->m_pNext;
	}
	else
	{
		for(CCommand *pCommand = m_pFirstCommand; pCommand->m_pNext; pCommand = pCommand->m_pNext)
			if(pCommand->m_pNext->m_Temp && str_comp(pCommand->m_pNext->m_pName, pName) == 0)
			{
				pRemoved = pCommand->m_pNext;
				pCommand->m_pNext = pCommand->m_pNext->m_pNext;
				break;
			}
	}

	// add to recycle list
	if(pRemoved)
	{
		pRemoved->m_pNext = m_pRecycleList;
		m_pRecycleList = pRemoved;
	}
}

void CConsole::DeregisterTempAll()
{
	// set non temp as first one
	for(; m_pFirstCommand && m_pFirstCommand->m_Temp; m_pFirstCommand = m_pFirstCommand->m_pNext)
		;

	// remove temp entries from command list
	for(CCommand *pCommand = m_pFirstCommand; pCommand && pCommand->m_pNext; pCommand = pCommand->m_pNext)
	{
		CCommand *pNext = pCommand->m_pNext;
		if(pNext->m_Temp)
		{
			for(; pNext && pNext->m_Temp; pNext = pNext->m_pNext)
				;
			pCommand->m_pNext = pNext;
		}
	}

	m_TempCommands.Reset();
	m_pRecycleList = 0;
}

void CConsole::Con_Chain(IResult *pResult, void *pUserData)
{
	CChain *pInfo = (CChain *)pUserData;
	pInfo->m_pfnChainCallback(pResult, pInfo->m_pUserData, pInfo->m_pfnCallback, pInfo->m_pCallbackUserData);
}

void CConsole::Chain(const char *pName, FChainCommandCallback pfnChainFunc, void *pUser)
{
	CCommand *pCommand = FindCommand(pName, m_FlagMask);

	if(!pCommand)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "failed to chain '%s'", pName);
		Print(IConsole::OUTPUT_LEVEL_DEBUG, "console", aBuf);
		return;
	}

	CChain *pChainInfo = new CChain();

	// store info
	pChainInfo->m_pfnChainCallback = pfnChainFunc;
	pChainInfo->m_pUserData = pUser;
	pChainInfo->m_pfnCallback = pCommand->m_pfnCallback;
	pChainInfo->m_pCallbackUserData = pCommand->m_pUserData;

	// chain
	pCommand->m_pfnCallback = Con_Chain;
	pCommand->m_pUserData = pChainInfo;
}

void CConsole::StoreCommands(bool Store)
{
	if(!Store)
	{
		for(CExecutionQueue::CQueueEntry *pEntry = m_ExecutionQueue.m_pFirst; pEntry; pEntry = pEntry->m_pNext)
			pEntry->m_pfnCommandCallback(&pEntry->m_Result, pEntry->m_pCommandUserData);
		m_ExecutionQueue.Reset();
	}
	m_StoreCommands = Store;
}

const IConsole::CCommandInfo *CConsole::GetCommandInfo(const char *pName, int FlagMask, bool Temp)
{
	for(CCommand *pCommand = m_pFirstCommand; pCommand; pCommand = pCommand->m_pNext)
	{
		if(pCommand->m_Flags & FlagMask && pCommand->m_Temp == Temp)
		{
			if(str_comp_nocase(pCommand->m_pName, pName) == 0)
				return pCommand;
		}
	}

	return 0;
}

extern IConsole *CreateConsole(int FlagMask) { return new CConsole(FlagMask); }

void CConsole::ResetServerGameSettings()
{
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Flags, Desc) \
	{ \
		if(((Flags) & (CFGFLAG_SERVER | CFGFLAG_GAME)) == (CFGFLAG_SERVER | CFGFLAG_GAME)) \
		{ \
			CCommand *pCommand = FindCommand(#ScriptName, CFGFLAG_SERVER); \
			void *pUserData = pCommand->m_pUserData; \
			FCommandCallback pfnCallback = pCommand->m_pfnCallback; \
			while(pfnCallback == Con_Chain) \
			{ \
				CChain *pChainInfo = (CChain *)pUserData; \
				pUserData = pChainInfo->m_pCallbackUserData; \
				pfnCallback = pChainInfo->m_pfnCallback; \
			} \
			CIntVariableData *pData = (CIntVariableData *)pUserData; \
			*pData->m_pVariable = pData->m_OldValue; \
		} \
	}

#define MACRO_CONFIG_COL(Name, ScriptName, Def, Save, Desc) MACRO_CONFIG_INT(Name, ScriptName, Def, 0, 0, Save, Desc)

#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Flags, Desc) \
	{ \
		if(((Flags) & (CFGFLAG_SERVER | CFGFLAG_GAME)) == (CFGFLAG_SERVER | CFGFLAG_GAME)) \
		{ \
			CCommand *pCommand = FindCommand(#ScriptName, CFGFLAG_SERVER); \
			void *pUserData = pCommand->m_pUserData; \
			FCommandCallback pfnCallback = pCommand->m_pfnCallback; \
			while(pfnCallback == Con_Chain) \
			{ \
				CChain *pChainInfo = (CChain *)pUserData; \
				pUserData = pChainInfo->m_pCallbackUserData; \
				pfnCallback = pChainInfo->m_pfnCallback; \
			} \
			CStrVariableData *pData = (CStrVariableData *)pUserData; \
			str_copy(pData->m_pOldValue, pData->m_pStr, pData->m_MaxSize); \
		} \
	}

#include "config_variables.h"

#undef MACRO_CONFIG_INT
#undef MACRO_CONFIG_COL
#undef MACRO_CONFIG_STR
}

int CConsole::CResult::GetVictim()
{
	return m_Victim;
}

void CConsole::CResult::ResetVictim()
{
	m_Victim = VICTIM_NONE;
}

bool CConsole::CResult::HasVictim()
{
	return m_Victim != VICTIM_NONE;
}

void CConsole::CResult::SetVictim(int Victim)
{
	m_Victim = clamp<int>(Victim, VICTIM_NONE, MAX_CLIENTS - 1);
}

void CConsole::CResult::SetVictim(const char *pVictim)
{
	if(!str_comp(pVictim, "me"))
		m_Victim = VICTIM_ME;
	else if(!str_comp(pVictim, "all"))
		m_Victim = VICTIM_ALL;
	else
		m_Victim = clamp<int>(str_toint(pVictim), 0, MAX_CLIENTS - 1);
}
