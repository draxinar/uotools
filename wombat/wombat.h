#ifndef WOMBAT_H
#define WOMBAT_H

/*
 * wombat.h - public interface for the Wombat bytecode codec.
 *
 * The token enum below is a direct port of the corresponding enum in
 * the UoDemo.exe decompilation. Tool-only entry points are
 * wombat_decode / wombat_encode - the original binary has no equivalent
 * of either, since it only parses bytecode for execution and never
 * generates it nor produces human-readable text.
 *
 * CScriptStringDB mirrors the binary's string database type. Its Load
 * (0x0040107D) and Get (0x00401381) methods in wombat.c are direct
 * ports of the UoDemo.exe decompilation; Find/Add/Save/Free have no
 * binary equivalent and are tool-only helpers.
 */

#include <stdint.h>

/*
 * Bytecode token type IDs (138 entries, 0x00-0x89). The variant-encoded
 * range uses g_TokenVariantTable; the trigger-name range (0x42-0x88) is
 * compared by string.
 */
enum {
	/* Punctuation / symbols (variant-encoded) */
	SM_LPAREN = 0x02,   /* ( */
	SM_RPAREN = 0x03,   /* ) */
	SM_COMMA = 0x04,    /* , */
	SM_SEMI = 0x06,     /* ; */
	SM_LBRACE = 0x07,   /* { */
	SM_RBRACE = 0x08,   /* } */
	SM_LBRACKET = 0x09, /* [ */
	SM_RBRACKET = 0x0A, /* ] */

	/* Operators (variant-encoded) */
	OP_NOT = 0x0B,    /* ! */
	OP_ADD = 0x0C,    /* + */
	OP_SUB = 0x0D,    /* - */
	OP_MUL = 0x0E,    /* * */
	OP_DIV = 0x0F,    /* / */
	OP_MOD = 0x10,    /* % */
	OP_ISEQ = 0x11,   /* == */
	OP_ISNEQ = 0x12,  /* != */
	OP_LT = 0x13,     /* < */
	OP_GT = 0x14,     /* > */
	OP_LTEQ = 0x15,   /* <= */
	OP_GTEQ = 0x16,   /* >= */
	OP_ASSIGN = 0x17, /* = */
	OP_LOGAND = 0x18, /* && */
	OP_LOGOR = 0x19,  /* || */
	OP_XOR = 0x1A,    /* ^ */
	OP_INC = 0x1B,    /* ++ */
	OP_DEC = 0x1C,    /* -- */

	/* Type keywords (variant-encoded) */
	TK_INT = 0x1D,
	TK_STRING = 0x1E,
	TK_USTRING = 0x1F, /* unsigned string */
	TK_LOC = 0x20,
	TK_OBJ = 0x21,
	TK_LIST = 0x22,
	TK_VOID = 0x23,

	/* Misc (variant-encoded) */
	T_OFFSET = 0x24,

	/* Control flow (variant-encoded) */
	TK_IF = 0x25,
	TK_ELSE = 0x26,
	TK_ENDIF = 0x27,
	TK_WHILE = 0x28,
	TK_ENDWHILE = 0x29,
	TK_FOR = 0x2A,
	TK_ENDFOR = 0x2B,
	TK_CONTINUE = 0x2C,
	TK_BREAK = 0x2D,
	TK_GOTO = 0x2E,
	TK_SWITCH = 0x2F,
	TK_ENDSWITCH = 0x30,
	TK_CASE = 0x31,
	TK_DEFAULT = 0x32,
	TK_RETURN = 0x33,

	/* Top-level declarations (variant-encoded) */
	TK_FUNCTION = 0x34,
	TK_TRIGGER = 0x35,
	TK_MEMBER = 0x36,
	TK_INHERITS = 0x37,
	TK_FORWARD = 0x38,

	/* Data tokens (variant-encoded) */
	T_STR = 0x3A,   /* string literal: 2-byte sdb index follows */
	T_BYTE = 0x3C,  /* 3 bytes inline data follows */
	T_WORD = 0x3D,  /* 4 bytes inline data follows */
	T_DWORD = 0x3E, /* 6 bytes inline data follows */
	T_ID = 0x41,    /* identifier: 2-byte sdb index follows */

	/* Text-based trigger names (string comparison, 0x42..0x88) */
	TR_SPEECH = 0x42,
	TR_GOTATTACKED = 0x43,
	TR_KILLEDTARGET = 0x44,
	TR_AVERSION = 0x45,
	TR_DEATH = 0x46,
	TR_SAWDEATH = 0x47,
	TR_FIGHTPULSE = 0x48,
	TR_WASHIT = 0x49,
	TR_FAILFOOD = 0x4A,
	TR_FAILDESIRE = 0x4B,
	TR_FAILSHELTER = 0x4C,
	TR_FOUNDFOOD = 0x4D,
	TR_FOUNDDESIRE = 0x4E,
	TR_FOUNDSHELTER = 0x4F,
	TR_TIME = 0x50,
	TR_CREATION = 0x51,
	TR_ENTERRANGE = 0x52,
	TR_LEAVERANGE = 0x53,
	TR_LOITER = 0x54,
	TR_SEEKFOOD = 0x55,
	TR_SEEKDESIRE = 0x56,
	TR_SEEKSHELTER = 0x57,
	TR_MESSAGE = 0x58,
	TR_USE = 0x59,
	TR_TARGETOBJ = 0x5A,
	TR_TARGETLOC = 0x5B,
	TR_WEATHER = 0x5C,
	TR_WASDROPPED = 0x5D,
	TR_LOOKEDAT = 0x5E,
	TR_GIVE = 0x5F,
	TR_WASGOTTEN = 0x60,
	TR_PATHFOUND = 0x61,
	TR_PATHNOTFOUND = 0x62,
	TR_CALLBACK = 0x63,
	TR_ISHITTING = 0x64,
	TR_CONVOFUNC = 0x65,
	TR_TYPESELECTED = 0x66,
	TR_HUESELECTED = 0x67,
	TR_MOON = 0x68,
	TR_MINRANGEATTACK = 0x69,
	TR_MINRANGEDEFEND = 0x6A,
	TR_MAXRANGEATTACK = 0x6B,
	TR_MAXRANGEDEFEND = 0x6C,
	TR_DESTROYED = 0x6D,
	TR_EQUIP = 0x6E,
	TR_UNEQUIP = 0x6F,
	TR_ISSTACKABLEON = 0x70,
	TR_STACKONTO = 0x71,
	TR_MULTIRECYCLE = 0x72,
	TR_DECAY = 0x73,
	TR_SERVERSWITCH = 0x74,
	TR_OORUSE = 0x75,
	TR_ACQUIREDESIRE = 0x76,
	TR_LOGOUT = 0x77,
	TR_OBJECTLOADED = 0x78,
	TR_GENERICGUMP = 0x79,
	TR_OORTARGETOBJ = 0x7A,
	TR_PKPOST = 0x7B,
	TR_TEXTENTRY = 0x7C,
	TR_SHOP = 0x7D,
	TR_STOLENFROM = 0x7E,
	TR_OBJACCESS = 0x7F,
	TR_ISHEALTHY = 0x80,
	TR_ONLINE = 0x81,
	TR_TRANSACCOUNTCHECK = 0x82,
	TR_TRANSRESPONSE = 0x83,
	TR_CANBUY = 0x84,
	TR_MOBISHITTING = 0x85,
	TR_FAMECHANGED = 0x86,
	TR_KARMACHANGED = 0x87,
	TR_MURDERCOUNTCHANGED = 0x88,

	TOKEN_TYPE_COUNT = 0x8A, /* g_MaxTokenType + 1 */
};

#define TRIGGER_EVENT_COUNT 71

/*
 * CScriptStringDB - string database loaded from sdb.txt.
 *
 * The UoDemo.exe binary stores entries as a CSdbStr vector; the tool
 * uses a flat char** + count/capacity since it only needs read/write
 * semantics for the codec. Load and Get are direct ports of the binary
 * decompilation; Save/Find/Add/Free are tool-only helpers.
 */
typedef struct CScriptStringDB CScriptStringDB;
struct CScriptStringDB {
	char **strings;
	int count;
	int capacity;
	int crlf; /* nonzero if source file used \r\n line endings */
};

/* 0x0040107D - CScriptStringDB::Load */
int CScriptStringDB_Load(CScriptStringDB *db, const char *path);

/* 0x00401381 - CScriptStringDB::Get */
const char *CScriptStringDB_Get(CScriptStringDB *db, int index);

/* Tool-only - no binary equivalent */
int CScriptStringDB_Save(CScriptStringDB *db, const char *path);
int CScriptStringDB_Find(const CScriptStringDB *db, const char *s);
int CScriptStringDB_Add(CScriptStringDB *db, const char *s);
void CScriptStringDB_Free(CScriptStringDB *db);

/*
 * Decode compiled .m bytecode to human-readable Wombat source text.
 * Tool-only - no binary equivalent.
 *
 * The binary's ScriptTokenizer_ReadToken (0x004283E4) produces token
 * buffers for the parser; this function additionally formats the stream
 * as readable source. The match loop mirrors ScriptTokenizer_ReadToken;
 * stream matching uses ScriptTokenizer_MatchToken verbatim.
 *
 * Returns 0 on success, non-zero on error.
 */
int wombat_decode(const char *inpath, const char *outpath, CScriptStringDB *db);

/*
 * Encode Wombat source text to compiled .m bytecode.
 * Tool-only - no binary equivalent.
 *
 * If refpath is non-NULL, variant encodings and token structure are
 * copied from the reference binary to produce a byte-identical output.
 *
 * Returns 0 on success, non-zero on error.
 */
int wombat_encode(const char *inpath, const char *outpath, CScriptStringDB *db,
        const char *refpath);

#endif /* WOMBAT_H */
