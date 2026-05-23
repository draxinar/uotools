/*
 * wombat - encoder/decoder for Wombat .m compiled script bytecode.
 *
 * Decodes compiled .m bytecode files into human-readable Wombat
 * source text, or encodes Wombat source text back into bytecode.
 *
 * The token enum, data tables, CScriptStringDB struct, and
 * ScriptTokenizer_MatchToken are direct ports of the corresponding
 * UoDemo.exe decompilation; each carries its UoDemo.exe address in
 * its top comment. The decoder, encoder, and source-text lexer have
 * no binary equivalent: the original binary never produces text output
 * and never generates bytecode from text.
 *
 * Token types:
 *   Variant-encoded: 2-byte uint16 matched against 5 possible values
 *   Text-based:      literal ASCII strings (trigger names like TR_USE)
 *   T_STR (0x3A):    2-byte token + 2-byte SDB index (chainable)
 *   T_ID  (0x41):    2-byte token + 2-byte SDB index
 *   T_BYTE (0x3C):   2-byte token + 1 byte inline data
 *   T_WORD (0x3D):   2-byte token + 2 bytes inline data
 *   T_DWORD (0x3E):  2-byte token + 4 bytes inline data
 *
 * Trigger event names (0x42..0x88) appear as text-based tokens in the
 * bytecode format specification, but the original compiler encoded them
 * as T_ID + SDB index references (using lowercase names like "lookedat"
 * stored in the string database). The decoder accepts both formats;
 * the encoder emits T_ID + SDB index to match the original compiler.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "wombat.h"

#define nelem(x) (sizeof(x) / sizeof((x)[0]))

#define MAX_SDB_STRINGS 16384

/*
 * Tool-only - no binary equivalent.
 *
 * tokenType to source-text mapping. Used by the decoder (output) and
 * the encoder (lex).
 */
static const struct {
	int id;
	const char *text;
} g_TokenText[] = {
	{ SM_LPAREN, "(" },
	{ SM_RPAREN, ")" },
	{ SM_COMMA, "," },
	{ SM_SEMI, ";" },
	{ SM_LBRACE, "{" },
	{ SM_RBRACE, "}" },
	{ SM_LBRACKET, "[" },
	{ SM_RBRACKET, "]" },
	{ OP_NOT, "!" },
	{ OP_ADD, "+" },
	{ OP_SUB, "-" },
	{ OP_MUL, "*" },
	{ OP_DIV, "/" },
	{ OP_MOD, "%" },
	{ OP_ISEQ, "==" },
	{ OP_ISNEQ, "!=" },
	{ OP_LT, "<" },
	{ OP_GT, ">" },
	{ OP_LTEQ, "<=" },
	{ OP_GTEQ, ">=" },
	{ OP_ASSIGN, "=" },
	{ OP_LOGAND, "&&" },
	{ OP_LOGOR, "||" },
	{ OP_XOR, "^" },
	{ OP_INC, "++" },
	{ OP_DEC, "--" },
	{ TK_INT, "int" },
	{ TK_STRING, "string" },
	{ TK_USTRING, "ustring" },
	{ TK_LOC, "loc" },
	{ TK_OBJ, "obj" },
	{ TK_LIST, "list" },
	{ TK_VOID, "void" },
	{ TK_IF, "if" },
	{ TK_ELSE, "else" },
	{ TK_ENDIF, "endif" },
	{ TK_WHILE, "while" },
	{ TK_ENDWHILE, "endwhile" },
	{ TK_FOR, "for" },
	{ TK_ENDFOR, "endfor" },
	{ TK_CONTINUE, "continue" },
	{ TK_BREAK, "break" },
	{ TK_GOTO, "goto" },
	{ TK_SWITCH, "switch" },
	{ TK_ENDSWITCH, "endswitch" },
	{ TK_CASE, "case" },
	{ TK_DEFAULT, "default" },
	{ TK_RETURN, "return" },
	{ TK_FUNCTION, "function" },
	{ TK_TRIGGER, "trigger" },
	{ TK_MEMBER, "member" },
	{ TK_INHERITS, "inherits" },
	{ TK_FORWARD, "forward" },
	{ -1, NULL },
};

/*
 * UoDemo.exe - 0x005EE45C - g_TriggerEventNames
 *
 * 71 entries, lowercase names matching the Wombat source trigger
 * syntax. Index 0 = "speech", index 70 = "murdercountchanged".
 *
 * The parser (0x00427436) uses strcmp against this table to find the
 * trigger event type index. DispatchEvent (0x0042B951) uses the same
 * indices for its jump table at 0x0042D7B8.
 */
static const char *g_TriggerEventNames[TRIGGER_EVENT_COUNT] = {
	"speech",             /* 0x00 */
	"gotattacked",        /* 0x01 */
	"killedtarget",       /* 0x02 */
	"aversion",           /* 0x03 */
	"death",              /* 0x04 */
	"sawdeath",           /* 0x05 */
	"fightpulse",         /* 0x06 */
	"washit",             /* 0x07 */
	"failfood",           /* 0x08 */
	"faildesire",         /* 0x09 */
	"failshelter",        /* 0x0A */
	"foundfood",          /* 0x0B */
	"founddesire",        /* 0x0C */
	"foundshelter",       /* 0x0D */
	"time",               /* 0x0E */
	"creation",           /* 0x0F */
	"enterrange",         /* 0x10 */
	"leaverange",         /* 0x11 */
	"loiter",             /* 0x12 */
	"seekfood",           /* 0x13 */
	"seekdesire",         /* 0x14 */
	"seekshelter",        /* 0x15 */
	"message",            /* 0x16 */
	"use",         /* 0x17 */
	"targetobj",          /* 0x18 */
	"targetloc",          /* 0x19 */
	"weather",            /* 0x1A */
	"wasdropped",         /* 0x1B */
	"lookedat",           /* 0x1C */
	"give",               /* 0x1D */
	"wasgotten",          /* 0x1E */
	"pathfound",          /* 0x1F */
	"pathnotfound",       /* 0x20 */
	"callback",           /* 0x21 */
	"ishitting",          /* 0x22 */
	"convofunc",          /* 0x23 */
	"typeselected",       /* 0x24 */
	"hueselected",        /* 0x25 */
	"moon",               /* 0x26 */
	"minrangeattack",     /* 0x27 */
	"minrangedefend",     /* 0x28 */
	"maxrangeattack",     /* 0x29 */
	"maxrangedefend",     /* 0x2A */
	"destroyed",          /* 0x2B */
	"equip",              /* 0x2C */
	"unequip",            /* 0x2D */
	"isstackableon",      /* 0x2E */
	"stackonto",          /* 0x2F */
	"multirecycle",       /* 0x30 */
	"decay",              /* 0x31 */
	"serverswitch",       /* 0x32 */
	"ooruse",             /* 0x33 */
	"acquiredesire",      /* 0x34 */
	"logout",             /* 0x35 */
	"objectloaded",       /* 0x36 */
	"genericgump",        /* 0x37 */
	"oortargetobj",       /* 0x38 */
	"pkpost",             /* 0x39 */
	"textentry",          /* 0x3A */
	"shop",               /* 0x3B */
	"stolenfrom",         /* 0x3C */
	"objaccess",          /* 0x3D */
	"ishealthy",          /* 0x3E */
	"online",             /* 0x3F */
	"transaccountcheck",  /* 0x40 */
	"transresponse",      /* 0x41 */
	"canbuy",             /* 0x42 */
	"mobishitting",       /* 0x43 */
	"famechanged",        /* 0x44 */
	"karmachanged",       /* 0x45 */
	"murdercountchanged", /* 0x46 */
};

/*
 * UoDemo.exe - 0x00610B40 - g_TokenVariants
 *
 * Variant encoding table: 10 bytes per entry (5 uint16), 138 entries.
 * Each token type can be encoded with any of its 5 variant values in
 * the compiled bytecode; MatchToken checks all 5. Only token types with
 * hasVariants=1 in g_TokenTypeTable use this. Text-based tokens
 * (trigger names, 0x42..0x88) use string comparison.
 */
static const uint16_t g_TokenVariants[TOKEN_TYPE_COUNT][5] = {
	[SM_LPAREN] = { 0x390C, 0x0F3E, 0x0199, 0x0124, 0x305E },
	[SM_RPAREN] = { 0x39B3, 0x2D12, 0x26A6, 0x5D03, 0x1238 },
	[SM_COMMA] = { 0x3B25, 0x1E1F, 0x1AD4, 0x7F96, 0x7FF5 },
	[SM_SEMI] = { 0x0732, 0x0120, 0x5CFD, 0x3E12, 0x3BF6 },
	[SM_LBRACE] = { 0x3A9E, 0x0DDC, 0x5E14, 0x2E40, 0x1CD0 },
	[SM_RBRACE] = { 0x7EB7, 0x6032, 0x2C3B, 0x15A1, 0x3EF6 },
	[SM_LBRACKET] = { 0x409D, 0x12E1, 0x121F, 0x26CA, 0x3699 },
	[SM_RBRACKET] = { 0x0902, 0x7BB9, 0x139D, 0x187E, 0x16C5 },
	[OP_NOT] = { 0x3CD5, 0x13E9, 0x4080, 0x5DB2, 0x33EA },
	[OP_ADD] = { 0x23C9, 0x60BF, 0x3CD6, 0x0FBF, 0x2F14 },
	[OP_SUB] = { 0x047E, 0x368E, 0x2FFF, 0x288F, 0x7DD1 },
	[OP_MUL] = { 0x261E, 0x5E9D, 0x1916, 0x32E6, 0x401D },
	[OP_DIV] = { 0x0384, 0x18D7, 0x0FC9, 0x0E12, 0x2833 },
	[OP_MOD] = { 0x249E, 0x2B0C, 0x11F4, 0x5DD5, 0x127E },
	[OP_ISEQ] = { 0x0135, 0x07CF, 0x1AF4, 0x0ECC, 0x01D3 },
	[OP_ISNEQ] = { 0x0E90, 0x3A2D, 0x37E6, 0x19D9, 0x252A },
	[OP_LT] = { 0x37E5, 0x1DC0, 0x1481, 0x4087, 0x2B01 },
	[OP_GT] = { 0x16D4, 0x3A8D, 0x7FBE, 0x0C7B, 0x0C15 },
	[OP_LTEQ] = { 0x3807, 0x0633, 0x251F, 0x1D18, 0x3492 },
	[OP_GTEQ] = { 0x19DA, 0x39CE, 0x3BB1, 0x3004, 0x1796 },
	[OP_ASSIGN] = { 0x1F16, 0x182F, 0x2CF7, 0x5ED0, 0x1316 },
	[OP_LOGAND] = { 0x5D24, 0x0588, 0x7CFE, 0x2725, 0x0DE5 },
	[OP_LOGOR] = { 0x13D3, 0x29D8, 0x0A28, 0x09CE, 0x3960 },
	[OP_XOR] = { 0x263D, 0x3B97, 0x4027, 0x138A, 0x282D },
	[OP_INC] = { 0x5CCD, 0x0940, 0x293B, 0x40A5, 0x1D11 },
	[OP_DEC] = { 0x2528, 0x0EA9, 0x3F0B, 0x3087, 0x3F97 },
	[TK_INT] = { 0x30F1, 0x3295, 0x01C1, 0x0CE1, 0x3EE9 },
	[TK_STRING] = { 0x3F9A, 0x30A7, 0x2DB5, 0x169A, 0x2FE7 },
	[TK_USTRING] = { 0x10D9, 0x0390, 0x2A38, 0x0728, 0x5C5E },
	[TK_LOC] = { 0x01E1, 0x1030, 0x1BD9, 0x159F, 0x2BA5 },
	[TK_OBJ] = { 0x28E2, 0x2F0C, 0x1289, 0x3382, 0x36C2 },
	[TK_LIST] = { 0x26B1, 0x1CDF, 0x27DA, 0x0E29, 0x113E },
	[TK_VOID] = { 0x2E39, 0x1D3F, 0x1D5E, 0x1FF1, 0x7E0E },
	[T_OFFSET] = { 0x06E3, 0x36A1, 0x0C1E, 0x2120, 0x1DCB },
	[TK_IF] = { 0x12C2, 0x1003, 0x0607, 0x0784, 0x2B0F },
	[TK_ELSE] = { 0x3305, 0x32E7, 0x212C, 0x018E, 0x3308 },
	[TK_ENDIF] = { 0x1EDC, 0x20A8, 0x37BE, 0x01EB, 0x123B },
	[TK_WHILE] = { 0x3106, 0x018C, 0x357E, 0x0A87, 0x5D2B },
	[TK_ENDWHILE] = { 0x03FA, 0x0AF0, 0x0786, 0x2332, 0x1295 },
	[TK_FOR] = { 0x7DAA, 0x2F0B, 0x1BFC, 0x13F5, 0x1ECA },
	[TK_ENDFOR] = { 0x0D9F, 0x388A, 0x15FD, 0x7CB8, 0x1AF6 },
	[TK_CONTINUE] = { 0x017B, 0x6014, 0x0E99, 0x33CD, 0x27D3 },
	[TK_BREAK] = { 0x7F0D, 0x04F0, 0x183A, 0x1FB4, 0x13A6 },
	[TK_GOTO] = { 0x190B, 0x3605, 0x20AD, 0x32CF, 0x2CD5 },
	[TK_SWITCH] = { 0x04B0, 0x1927, 0x08FF, 0x31D8, 0x0914 },
	[TK_ENDSWITCH] = { 0x13F4, 0x3A27, 0x387C, 0x32C1, 0x198C },
	[TK_CASE] = { 0x3223, 0x17B8, 0x3895, 0x248D, 0x342D },
	[TK_DEFAULT] = { 0x5D3D, 0x3260, 0x32DE, 0x2780, 0x31AD },
	[TK_RETURN] = { 0x5DE9, 0x5EA5, 0x11D5, 0x199F, 0x2F15 },
	[TK_FUNCTION] = { 0x0E01, 0x19FE, 0x3821, 0x0B93, 0x0A2F },
	[TK_TRIGGER] = { 0x09B3, 0x038F, 0x328A, 0x08AF, 0x5CCA },
	[TK_MEMBER] = { 0x0C95, 0x7CBE, 0x7C27, 0x5D2A, 0x2FA1 },
	[TK_INHERITS] = { 0x31BE, 0x15B4, 0x07C9, 0x27C0, 0x1B32 },
	[TK_FORWARD] = { 0x2934, 0x3E09, 0x012C, 0x2CC6, 0x7FA6 },
	[T_STR] = { 0x5D17, 0x0A1D, 0x3B29, 0x2BFA, 0x2BB8 },
	[T_BYTE] = { 0x17BD, 0x21EB, 0x2015, 0x5DB8, 0x15E1 },
	[T_WORD] = { 0x5B60, 0x3D8F, 0x0FF4, 0x275B, 0x3C8A },
	[T_DWORD] = { 0x188F, 0x5D27, 0x7F5C, 0x01F7, 0x093B },
	[T_ID] = { 0x3510, 0x0B9B, 0x06E9, 0x3B9E, 0x0B31 },
	[0x89] = { 0x2AEA, 0x0860, 0x403E, 0x3925, 0x16F2 },
};

/*
 * UoDemo.exe - 0x00611318 - g_TokenTypeTable[].str (rows 0x42..0x88)
 *
 * Trigger name strings - text-based token names. In UoDemo.exe these
 * live as the .str field (offset +0x08) of g_TokenTypeTable's 12-byte
 * entries for tokens 0x42..0x88; the tool exposes them as a flat array
 * indexed by token type. They appear literally in compiled bytecodes;
 * MatchToken uses strncmp.
 */
static const char *g_TriggerNames[TOKEN_TYPE_COUNT] = {
	[TR_SPEECH] = "TR_SPEECH",
	[TR_GOTATTACKED] = "TR_GOTATTACKED",
	[TR_KILLEDTARGET] = "TR_KILLEDTARGET",
	[TR_AVERSION] = "TR_AVERSION",
	[TR_DEATH] = "TR_DEATH",
	[TR_SAWDEATH] = "TR_SAWDEATH",
	[TR_FIGHTPULSE] = "TR_FIGHTPULSE",
	[TR_WASHIT] = "TR_WASHIT",
	[TR_FAILFOOD] = "TR_FAILFOOD",
	[TR_FAILDESIRE] = "TR_FAILDESIRE",
	[TR_FAILSHELTER] = "TR_FAILSHELTER",
	[TR_FOUNDFOOD] = "TR_FOUNDFOOD",
	[TR_FOUNDDESIRE] = "TR_FOUNDDESIRE",
	[TR_FOUNDSHELTER] = "TR_FOUNDSHELTER",
	[TR_TIME] = "TR_TIME",
	[TR_CREATION] = "TR_CREATION",
	[TR_ENTERRANGE] = "TR_ENTERRANGE",
	[TR_LEAVERANGE] = "TR_LEAVERANGE",
	[TR_LOITER] = "TR_LOITER",
	[TR_SEEKFOOD] = "TR_SEEKFOOD",
	[TR_SEEKDESIRE] = "TR_SEEKDESIRE",
	[TR_SEEKSHELTER] = "TR_SEEKSHELTER",
	[TR_MESSAGE] = "TR_MESSAGE",
	[TR_USE] = "TR_USE",
	[TR_TARGETOBJ] = "TR_TARGETOBJ",
	[TR_TARGETLOC] = "TR_TARGETLOC",
	[TR_WEATHER] = "TR_WEATHER",
	[TR_WASDROPPED] = "TR_WASDROPPED",
	[TR_LOOKEDAT] = "TR_LOOKEDAT",
	[TR_GIVE] = "TR_GIVE",
	[TR_WASGOTTEN] = "TR_WASGOTTEN",
	[TR_PATHFOUND] = "TR_PATHFOUND",
	[TR_PATHNOTFOUND] = "TR_PATHNOTFOUND",
	[TR_CALLBACK] = "TR_CALLBACK",
	[TR_ISHITTING] = "TR_ISHITTING",
	[TR_CONVOFUNC] = "TR_CONVOFUNC",
	[TR_TYPESELECTED] = "TR_TYPESELECTED",
	[TR_HUESELECTED] = "TR_HUESELECTED",
	[TR_MOON] = "TR_MOON",
	[TR_MINRANGEATTACK] = "TR_MINRANGEATTACK",
	[TR_MINRANGEDEFEND] = "TR_MINRANGEDEFEND",
	[TR_MAXRANGEATTACK] = "TR_MAXRANGEATTACK",
	[TR_MAXRANGEDEFEND] = "TR_MAXRANGEDEFEND",
	[TR_DESTROYED] = "TR_DESTROYED",
	[TR_EQUIP] = "TR_EQUIP",
	[TR_UNEQUIP] = "TR_UNEQUIP",
	[TR_ISSTACKABLEON] = "TR_ISSTACKABLEON",
	[TR_STACKONTO] = "TR_STACKONTO",
	[TR_MULTIRECYCLE] = "TR_MULTIRECYCLE",
	[TR_DECAY] = "TR_DECAY",
	[TR_SERVERSWITCH] = "TR_SERVERSWITCH",
	[TR_OORUSE] = "TR_OORUSE",
	[TR_ACQUIREDESIRE] = "TR_ACQUIREDESIRE",
	[TR_LOGOUT] = "TR_LOGOUT",
	[TR_OBJECTLOADED] = "TR_OBJECTLOADED",
	[TR_GENERICGUMP] = "TR_GENERICGUMP",
	[TR_OORTARGETOBJ] = "TR_OORTARGETOBJ",
	[TR_PKPOST] = "TR_PKPOST",
	[TR_TEXTENTRY] = "TR_TEXTENTRY",
	[TR_SHOP] = "TR_SHOP",
	[TR_STOLENFROM] = "TR_STOLENFROM",
	[TR_OBJACCESS] = "TR_OBJACCESS",
	[TR_ISHEALTHY] = "TR_ISHEALTHY",
	[TR_ONLINE] = "TR_ONLINE",
	[TR_TRANSACCOUNTCHECK] = "TR_TRANSACCOUNTCHECK",
	[TR_TRANSRESPONSE] = "TR_TRANSRESPONSE",
	[TR_CANBUY] = "TR_CANBUY",
	[TR_MOBISHITTING] = "TR_MOBISHITTING",
	[TR_FAMECHANGED] = "TR_FAMECHANGED",
	[TR_KARMACHANGED] = "TR_KARMACHANGED",
	[TR_MURDERCOUNTCHANGED] = "TR_MURDERCOUNTCHANGED",
};

/*
 * UoDemo.exe - 0x0040107D - CScriptStringDB::Load
 *
 * Reloads the DB from path: clears the existing string vector then
 * reads each line (stripping trailing CR/LF) and appends it as a
 * CSdbStr entry. Returns 1 when the file cannot be opened, 0 on
 * success.
 *
 * The tool uses a char** vector instead of CSdbStr; the CRLF
 * detection for Save is a tool-only extension (the binary is
 * read-only and never writes the DB back).
 */
int
CScriptStringDB_Load(CScriptStringDB *db, const char *path)
{
	FILE *f;
	char line[1024];
	int len;
	int count;

	f = fopen(path, "r");
	if (f == NULL)
		return 1;

	CScriptStringDB_Free(db);

	db->capacity = MAX_SDB_STRINGS;
	db->strings = calloc(db->capacity, sizeof(char *));
	if (db->strings == NULL) {
		fclose(f);
		return 1;
	}
	count = 0;

	while (fgets(line, sizeof(line), f) != NULL) {
		/* Tool-only: detect CRLF on first line for Save round-trip */
		len = strlen(line);
		if (count == 0 && len >= 2 && line[len - 1] == '\n' &&
		        line[len - 2] == '\r')
			db->crlf = 1;

		// FIXED: binary reads line[-1] when len==0 (harmless no-op
		// on Windows stack, but ASAN catches the UB).
		len = strlen(line);
		if (len > 0 && line[len - 1] == '\n')
			line[len - 1] = '\0';

		len = strlen(line);
		if (len > 0 && line[len - 1] == '\r')
			line[len - 1] = '\0';

		if (count >= db->capacity) {
			int newcap;
			char **p;

			newcap = db->capacity * 2;
			p = realloc(db->strings, newcap * sizeof(char *));
			if (p == NULL)
				break;
			memset(p + db->capacity, 0,
			        (newcap - db->capacity) * sizeof(char *));
			db->strings = p;
			db->capacity = newcap;
		}

		db->strings[count] = strdup(line);
		count++;
	}

	db->count = count;
	fclose(f);
	return 0;
}

/*
 * UoDemo.exe - 0x00401381 - CScriptStringDB::Get
 *
 * Returns the C-string for the index-th SDB entry. The caller must
 * ensure index is in range; no bounds check is performed.
 *
 * The tool's representation stores the C-string directly (char *);
 * the binary stores CSdbStr (CString wrapper) and calls c_str().
 */
const char *
CScriptStringDB_Get(CScriptStringDB *db, int index)
{
	return db->strings[index];
}

/*
 * UoDemo.exe - 0x0042B1B0 - ScriptTokenizer_MatchToken
 *
 * Returns 1 when the bytecode at stream matches tokenType. Variant-
 * encoded tokens compare the leading 16-bit word against any of the
 * type's five permitted encodings; text-based tokens (trigger names)
 * compare by string prefix.
 */
int
ScriptTokenizer_MatchToken(const char *stream, int tokenType)
{
	uint16_t streamVal;
	int i;

	if (tokenType < 0 || tokenType >= TOKEN_TYPE_COUNT)
		return 0;

	// Check if this token type has variant encoding
	if (g_TokenVariants[tokenType][0] != 0) {
		// Variant-encoded: compare uint16 at stream against all 5 variants
		memcpy(&streamVal, stream, 2);
		for (i = 0; i < 5; i++) {
			if (g_TokenVariants[tokenType][i] == streamVal)
				return 1;
		}
		return 0;
	}

	// Text-based token (trigger names): compare string
	if (g_TriggerNames[tokenType] != NULL) {
		int len = strlen(g_TriggerNames[tokenType]);
		if (memcmp(stream, g_TriggerNames[tokenType], len) == 0)
			return 1;
	}

	return 0;
}

/*
 * Tool-only - no binary equivalent.
 *
 * Returns the index of the matching entry, or -1 when not found. Used
 * by the encoder to avoid creating duplicate SDB entries.
 */
int
CScriptStringDB_Find(const CScriptStringDB *db, const char *s)
{
	int i;
	for (i = 0; i < db->count; i++) {
		if (db->strings[i] != NULL && strcmp(db->strings[i], s) == 0)
			return i;
	}
	return -1;
}

/*
 * Tool-only - no binary equivalent.
 *
 * Used by the encoder when a string isn't already present in the DB.
 * Grows the vector if needed.
 */
int
CScriptStringDB_Add(CScriptStringDB *db, const char *s)
{
	if (db->count >= db->capacity) {
		int newcap;
		char **p;

		newcap = db->capacity * 2;
		p = realloc(db->strings, newcap * sizeof(char *));
		if (p == NULL)
			return -1;
		memset(p + db->capacity, 0,
		        (newcap - db->capacity) * sizeof(char *));
		db->strings = p;
		db->capacity = newcap;
	}

	db->strings[db->count] = strdup(s);
	if (db->strings[db->count] == NULL)
		return -1;
	return db->count++;
}

/*
 * Tool-only - no binary equivalent.
 *
 * Used by the encoder to persist new SDB entries it created. EOL
 * respects the CRLF flag captured by Load so the file format
 * round-trips.
 */
int
CScriptStringDB_Save(CScriptStringDB *db, const char *path)
{
	FILE *f;
	int i;
	const char *eol;

	eol = db->crlf ? "\r\n" : "\n";
	f = fopen(path, "wb");
	if (f == NULL)
		return -1;
	for (i = 0; i < db->count; i++) {
		if (db->strings[i] != NULL)
			fprintf(f, "%s%s", db->strings[i], eol);
		else
			fprintf(f, "%s", eol);
	}
	fclose(f);
	return 0;
}

/*
 * Tool-only - no binary equivalent.
 *
 * The binary's CSdbStr destructor runs implicitly during vector
 * teardown; the tool has to walk the array explicitly.
 */
void
CScriptStringDB_Free(CScriptStringDB *db)
{
	int i;
	if (db->strings != NULL) {
		for (i = 0; i < db->count; i++)
			free(db->strings[i]);
		free(db->strings);
		db->strings = NULL;
	}
	db->count = 0;
	db->capacity = 0;
}

/*
 * Tool-only - no binary equivalent.
 *
 * The binary walks the bytecode forward via ScriptTokenizer_ReadToken
 * and never needs this reverse map; the tool's decoder/formatter does,
 * since it dispatches output formatting on the resolved token type.
 */
static int
LookupTokenVariant(uint16_t v)
{
	int t, i;

	for (t = 0; t < TOKEN_TYPE_COUNT; t++) {
		if (g_TokenVariants[t][0] == 0)
			continue;
		for (i = 0; i < 5; i++) {
			if (g_TokenVariants[t][i] == v)
				return t;
		}
	}
	return -1;
}

/*
 * Tool-only - no binary equivalent.
 *
 * Returns the matching token type (0x42..0x88) if stream starts with
 * one of TR_SPEECH..TR_MURDERCOUNTCHANGED, or -1 otherwise. Used by
 * the decoder to detect text-based tokens before falling back to
 * variant lookup.
 */
static int
LookupTriggerName(const char *stream, int remaining)
{
	int t;

	for (t = 0x42; t <= 0x88; t++) {
		if (g_TriggerNames[t] != NULL) {
			int len = strlen(g_TriggerNames[t]);
			if (remaining >= len &&
			        memcmp(stream, g_TriggerNames[t], len) == 0)
				return t;
		}
	}
	return -1;
}

/*
 * Tool-only - no binary equivalent.
 *
 * Used by the decoder to drive context-sensitive formatting (e.g.
 * detecting "} else"). The binary walks the stream linearly; it has
 * no need to peek.
 */
static int
PeekTokenType(const char *p, const char *end)
{
	int remaining;
	int t;
	uint16_t v;

	remaining = (int)(end - p);

	t = LookupTriggerName(p, remaining);
	if (t >= 0)
		return t;
	if (remaining < 2)
		return -1;
	memcpy(&v, p, 2);
	return LookupTokenVariant(v);
}

/*
 * Tool-only - no binary equivalent.
 *
 * Mutable state threaded through the decoder's formatting helpers.
 * Indentation is deferred until content is actually written, so blank
 * lines never carry trailing whitespace.
 */
typedef struct DecoderState DecoderState;
struct DecoderState {
	FILE *fout;
	int indent;
	int prev; /* previous token type */
	int pending_nl; /* newline is pending before next token */
	int col; /* >0 means we have content on current line */
	int paren_depth; /* nesting depth of parentheses */
	int deferred_ind; /* >= 0: indent tabs to write before next content */
};

/*
 * Tool-only - no binary equivalent.
 *
 * Returns 1 if the previous token suppresses a trailing space (attaches
 * right to the next token).
 */
static int
no_space_after(int toktype)
{
	return toktype == SM_LPAREN || toktype == SM_LBRACKET ||
	       toktype == SM_RPAREN || toktype == OP_NOT || toktype == T_ID ||
	       toktype == T_STR;
}

/*
 * Tool-only - no binary equivalent.
 *
 * Called before any visible output to s->fout.
 */
static void
flush_indent(DecoderState *s)
{
	int i;

	if (s->deferred_ind >= 0) {
		for (i = 0; i < s->deferred_ind; i++)
			fputc('\t', s->fout);
		s->deferred_ind = -1;
	}
}

/*
 * Tool-only - no binary equivalent.
 *
 * '}' handles its own newline logic and doesn't use this.
 */
static void
flush_nl(DecoderState *s)
{
	if (s->pending_nl) {
		fputc('\n', s->fout);
		s->deferred_ind = s->indent;
		s->pending_nl = 0;
		s->col = 0;
	}
}

/*
 * Tool-only - no binary equivalent.
 *
 * Like flush_nl, but at indent level 0 top-level declarations (function,
 * trigger, inherits, member, forward) get an extra blank line separator.
 */
static void
flush_nl_top(DecoderState *s, int tt)
{
	if (s->pending_nl) {
		if (s->indent == 0 &&
		        (tt == TK_FUNCTION || tt == TK_TRIGGER ||
		                tt == TK_MEMBER || tt == TK_INHERITS ||
		                tt == TK_FORWARD)) {
			fputc('\n', s->fout);
		}
		fputc('\n', s->fout);
		s->deferred_ind = s->indent;
		s->pending_nl = 0;
		s->col = 0;
	}
}

/*
 * Tool-only - no binary equivalent.
 *
 * No space after '(' or '!', no space before ')' ',' ';' '++' '--'.
 */
static void
emit_space(DecoderState *s)
{
	flush_indent(s);
	if (s->col > 0 && !no_space_after(s->prev))
		fputc(' ', s->fout);
}

/*
 * wombat_decode - Tool-only. No binary equivalent.
 *
 * The binary's ScriptTokenizer_ReadToken (0x004283E4) emits token buffers
 * for the parser; this function additionally formats the bytecode stream
 * as human-readable source text with indent and whitespace rules.
 *
 * The match-loop dispatch order (T_STR chain -> T_ID -> T_BYTE -> T_WORD
 * -> T_DWORD -> variant lookup) mirrors ScriptTokenizer_ReadToken;
 * token matching uses ScriptTokenizer_MatchToken verbatim. Output style
 * uses a "pending newline" approach: after ';' and '{', pending_nl=1;
 * before any non-structural token the newline is flushed with the
 * current indent.
 */
int
wombat_decode(const char *inpath, const char *outpath, CScriptStringDB *db)
{
	FILE *fin, *fout;
	char *data;
	long len;
	const char *p, *end;
	DecoderState s;

	fin = fopen(inpath, "rb");
	if (fin == NULL) {
		fprintf(stderr, "wombat: cannot open %s\n", inpath);
		return 1;
	}

	fseek(fin, 0, SEEK_END);
	len = ftell(fin);
	fseek(fin, 0, SEEK_SET);
	if (len < 0) {
		fclose(fin);
		fprintf(stderr, "wombat: cannot size %s\n", inpath);
		return 1;
	}

	data = malloc((size_t)len + 1);
	if (data == NULL) {
		fclose(fin);
		return 1;
	}
	if (fread(data, 1, (size_t)len, fin) != (size_t)len) {
		free(data);
		fclose(fin);
		fprintf(stderr, "wombat: short read on %s\n", inpath);
		return 1;
	}
	data[len] = '\0';
	fclose(fin);

	fout = fopen(outpath, "w");
	if (fout == NULL) {
		free(data);
		fprintf(stderr, "wombat: cannot create %s\n", outpath);
		return 1;
	}

	p = data;
	end = data + len;
	s.fout = fout;
	s.indent = 0;
	s.prev = -1;
	s.pending_nl = 0;
	s.col = 0;
	s.paren_depth = 0;
	s.deferred_ind = -1;

	while (p < end && *p != '\0') {
		uint16_t v;
		int toktype;
		int remaining;
		remaining = (int)(end - p);

		toktype = LookupTriggerName(p, remaining);
		if (toktype >= 0) {
			int nlen;
			int evtIdx;
			nlen = strlen(g_TriggerNames[toktype]);
			evtIdx = toktype - 0x42;

			flush_nl(&s);
			emit_space(&s);
			if (evtIdx >= 0 &&
			        evtIdx < (int)nelem(g_TriggerEventNames))
				fputs(g_TriggerEventNames[evtIdx], fout);
			else
				fputs(g_TriggerNames[toktype], fout);
			p += nlen;
			s.prev = toktype;
			s.col = 1;
			continue;
		}

		if (remaining < 2)
			break;

		memcpy(&v, p, 2);
		toktype = LookupTokenVariant(v);

		/*
		 * T_STR: string literal - chainable.
		 * Replicate the binary's T_STR handler: unconditionally
		 * skip first character of each SDB entry (str + 1),
		 * strip trailing '"' before appending. Most entries
		 * start with '"' (e.g. "teleport"), but L"..." wide
		 * string entries start with 'L'.
		 */
		if (toktype == T_STR) {
			uint16_t idx;
			const char *str;

			flush_nl(&s);
			emit_space(&s);
			fputc('"', fout);

			while (p + 4 <= end &&
			        ScriptTokenizer_MatchToken(p, T_STR)) {
				p += 2;
				memcpy(&idx, p, 2);
				p += 2;
				str = CScriptStringDB_Get(db, idx);
				if (str != NULL && *str != '\0') {
					const char *c;
					int slen;

					/* Skip first char - binary does
					 * str + 1 unconditionally */
					c = str + 1;

					/* Strip trailing '"', escaping any
					 * embedded quotes/backslashes */
					slen = strlen(c);
					if (slen > 0 && c[slen - 1] == '"')
						slen--;
					for (int j = 0; j < slen; j++) {
						if (c[j] == '"' || c[j] == '\\')
							fputc('\\', fout);
						fputc(c[j], fout);
					}
				}
			}

			fputc('"', fout);
			s.prev = T_STR;
			s.col = 1;
			continue;
		}

		if (toktype == T_ID) {
			uint16_t idx;
			const char *str;

			if (p + 4 > end)
				break;
			p += 2;
			memcpy(&idx, p, 2);
			p += 2;
			str = CScriptStringDB_Get(db, idx);

			flush_nl(&s);
			emit_space(&s);
			if (str != NULL)
				fputs(str, fout);
			else
				fprintf(fout, "SDB_%d", idx);
			s.prev = T_ID;
			s.col = 1;
			continue;
		}

		/* T_BYTE: 1 byte inline data */
		if (toktype == T_BYTE) {
			unsigned char val;

			if (p + 3 > end)
				break;
			val = (unsigned char)p[2];
			flush_nl(&s);
			emit_space(&s);
			fprintf(fout, "0x%02X", val);
			p += 3;
			/* After case value, emit pending newline */
			if (s.prev == TK_CASE)
				s.pending_nl = 1;
			s.prev = T_BYTE;
			s.col = 1;
			continue;
		}

		/* T_WORD: 2 bytes inline data */
		if (toktype == T_WORD) {
			uint16_t val;

			if (p + 4 > end)
				break;
			memcpy(&val, p + 2, 2);
			flush_nl(&s);
			emit_space(&s);
			fprintf(fout, "0x%04X", val);
			p += 4;
			if (s.prev == TK_CASE)
				s.pending_nl = 1;
			s.prev = T_WORD;
			s.col = 1;
			continue;
		}

		/* T_DWORD: 4 bytes inline data */
		if (toktype == T_DWORD) {
			uint32_t val;

			if (p + 6 > end)
				break;
			memcpy(&val, p + 2, 4);
			flush_nl(&s);
			emit_space(&s);
			fprintf(fout, "0x%08X", val);
			p += 6;
			if (s.prev == TK_CASE)
				s.pending_nl = 1;
			s.prev = T_DWORD;
			s.col = 1;
			continue;
		}

		/* T_OFFSET: skip offset marker */
		if (toktype == T_OFFSET) {
			p += 2;
			continue;
		}

		if (toktype >= 0) {
			const char *text = NULL;
			int i;

			for (i = 0; g_TokenText[i].text != NULL; i++) {
				if (g_TokenText[i].id == toktype) {
					text = g_TokenText[i].text;
					break;
				}
			}

			p += 2;

			if (text == NULL) {
				flush_nl(&s);
				emit_space(&s);
				fprintf(fout, "?0x%02X", toktype);
				s.prev = toktype;
				s.col = 1;
				continue;
			}

			switch (toktype) {
			case SM_SEMI:
				if (s.paren_depth == 0)
					flush_nl(&s);
				flush_indent(&s);
				fputs(";", fout);
				if (s.paren_depth == 0)
					s.pending_nl = 1;
				s.prev = SM_SEMI;
				s.col = 1;
				break;

			case SM_LBRACE:
				flush_nl(&s);
				flush_indent(&s);
				if (s.col > 0)
					fputc(' ', fout);
				fputs("{", fout);
				s.indent++;
				s.pending_nl = 1;
				s.prev = SM_LBRACE;
				s.col = 1;
				break;

			case SM_RBRACE:
				s.indent--;
				if (s.indent < 0)
					s.indent = 0;
				if (s.pending_nl) {
					s.pending_nl = 0;
					fputc('\n', fout);
					s.deferred_ind = s.indent;
					s.col = 0;
				}
				flush_indent(&s);

				/* Check if next is 'else'-> } else { */
				{
					int next;

					next = PeekTokenType(p, end);
					if (next == TK_ELSE) {
						fputs("}", fout);
						/* leave col=1, don't set
						 * pending_nl; the 'else' case
						 * will add space */
					} else {
						fputs("}", fout);
						s.pending_nl = 1;
					}
				}
				s.prev = SM_RBRACE;
				s.col = 1;
				break;

			case SM_LPAREN:
				flush_nl(&s);
				flush_indent(&s);
				/* Space before ( after keywords like
				 * if/while/for. No space after identifier
				 * (function call), switch, return, or trigger
				 * event names. */
				if (s.prev == TK_IF || s.prev == TK_WHILE ||
				        s.prev == TK_FOR)
					fputc(' ', fout);
				else if (s.col > 0 && s.prev != T_ID &&
				         s.prev != TK_SWITCH &&
				         s.prev != TK_RETURN &&
				         !(s.prev >= 0x42 && s.prev <= 0x88) &&
				         !no_space_after(s.prev))
					fputc(' ', fout);
				fputs("(", fout);
				s.paren_depth++;
				s.prev = SM_LPAREN;
				s.col = 1;
				break;

			case SM_RPAREN:
				flush_indent(&s);
				/* Space before ) after type keywords
				 * (unnamed params in forward decls) or
				 * trailing comma. */
				if (s.prev == TK_INT || s.prev == TK_STRING ||
				        s.prev == TK_USTRING ||
				        s.prev == TK_LOC || s.prev == TK_OBJ ||
				        s.prev == TK_LIST ||
				        s.prev == TK_VOID || s.prev == SM_COMMA)
					fputc(' ', fout);
				fputs(")", fout);
				if (s.paren_depth > 0)
					s.paren_depth--;
				s.prev = SM_RPAREN;
				s.col = 1;
				break;

			case SM_COMMA:
				flush_indent(&s);
				/* Space before , after type keywords
				 * (unnamed params in forward decls). */
				if (s.prev == TK_INT || s.prev == TK_STRING ||
				        s.prev == TK_USTRING ||
				        s.prev == TK_LOC || s.prev == TK_OBJ ||
				        s.prev == TK_LIST || s.prev == TK_VOID)
					fputc(' ', fout);
				fputs(",", fout);
				s.prev = SM_COMMA;
				s.col = 1;
				break;

			case SM_LBRACKET:
				flush_indent(&s);
				fputs("[", fout);
				s.prev = SM_LBRACKET;
				s.col = 1;
				break;

			case SM_RBRACKET:
				flush_indent(&s);
				fputs("]", fout);
				s.prev = SM_RBRACKET;
				s.col = 1;
				break;

			case OP_INC:
			case OP_DEC:
				flush_indent(&s);
				fputs(text, fout);
				s.prev = toktype;
				s.col = 1;
				break;

			case OP_NOT:
				flush_nl(&s);
				emit_space(&s);
				fputs(text, fout);
				s.prev = toktype;
				s.col = 1;
				break;

			case OP_ADD:
			case OP_SUB:
			case OP_MUL:
			case OP_DIV:
			case OP_MOD:
			case OP_ISEQ:
			case OP_ISNEQ:
			case OP_LT:
			case OP_GT:
			case OP_LTEQ:
			case OP_GTEQ:
			case OP_ASSIGN:
			case OP_LOGAND:
			case OP_LOGOR:
			case OP_XOR:
				flush_indent(&s);
				fprintf(fout, " %s", text);
				s.prev = toktype;
				s.col = 1;
				break;

			case TK_CASE:
				/* case labels are outdented by 1
				 * relative to the switch body. */
				if (s.pending_nl) {
					int ci;

					ci = s.indent > 0 ? s.indent - 1 : 0;
					fputc('\n', fout);
					s.deferred_ind = ci;
					s.pending_nl = 0;
					s.col = 0;
				}
				flush_indent(&s);
				if (s.col > 0)
					fputc(' ', fout);
				fputs(text, fout);
				s.prev = toktype;
				s.col = 1;
				break;

			case TK_DEFAULT:
				if (s.paren_depth > 0 || s.prev == TK_INT ||
				        s.prev == TK_STRING ||
				        s.prev == TK_USTRING ||
				        s.prev == TK_LOC || s.prev == TK_OBJ ||
				        s.prev == TK_LIST ||
				        s.prev == TK_VOID) {
					/* Variable name or expression */
					flush_nl(&s);
					emit_space(&s);
					fputs(text, fout);
				} else if (s.prev == T_BYTE ||
				           s.prev == T_WORD ||
				           s.prev == T_DWORD) {
					/* Combined case/default label -
					 * emit at body indent */
					if (s.pending_nl) {
						fputc('\n', fout);
						s.deferred_ind = s.indent;
						s.pending_nl = 0;
						s.col = 0;
					}
					flush_indent(&s);
					if (s.col > 0)
						fputc(' ', fout);
					fputs(text, fout);
					s.pending_nl = 1;
				} else {
					/* Normal switch label - outdent */
					if (s.pending_nl) {
						int ci;

						ci = s.indent > 0 ? s.indent - 1
						                  : 0;
						fputc('\n', fout);
						s.deferred_ind = ci;
						s.pending_nl = 0;
						s.col = 0;
					}
					flush_indent(&s);
					if (s.col > 0)
						fputc(' ', fout);
					fputs(text, fout);
					s.pending_nl = 1;
				}
				s.prev = toktype;
				s.col = 1;
				break;

			case TK_MEMBER:
				/* Member declarations always appear at
				 * indent 0 with a blank-line separator. */
				if (s.pending_nl) {
					fputc('\n', fout);
					fputc('\n', fout);
					s.deferred_ind = 0;
					s.pending_nl = 0;
					s.col = 0;
				}
				flush_indent(&s);
				fputs(text, fout);
				s.prev = toktype;
				s.col = 1;
				break;

			default:
				flush_nl_top(&s, toktype);
				emit_space(&s);
				fputs(text, fout);
				s.prev = toktype;
				s.col = 1;
				break;
			}
			continue;
		}

		/* Unknown 2-byte value */
		flush_nl(&s);
		emit_space(&s);
		fprintf(fout, "??0x%04X", v);
		p += 2;
		s.prev = -1;
		s.col = 1;
	}

	if (s.col > 0)
		fputc('\n', fout);

	fclose(fout);
	free(data);
	return 0;
}

/*
 * Encode: text to bytecode. Tool-only - no binary equivalent.
 *
 * The original binary only parses bytecode for execution and never generates it;
 * this entire section (the source lexer, the emit_* helpers, and the
 * RefBin layer) has no canonical equivalent. Its only correctness
 * constraint is that ScriptTokenizer_MatchToken / ScriptTokenizer_ReadToken
 * accept the output.
 */

/*
 * Reference binary for preserving variant encoding during re-encode.
 * When provided, the encoder copies variant bytes and T_OFFSET tokens
 * from the reference to produce a byte-identical round-trip.
 */
typedef struct RefBin RefBin;
struct RefBin {
	const char *data;
	const char *end;
	const char *p;
};

/*
 * Copy any T_OFFSET tokens at the current reference position to output.
 * T_OFFSET is silently dropped during decode, so the encoder must
 * re-insert them from the reference to achieve exact binary match.
 */
static void
ref_copy_offsets(RefBin *ref, FILE *f)
{
	if (ref == NULL)
		return;
	while (ref->p + 2 <= ref->end &&
	        ScriptTokenizer_MatchToken(ref->p, T_OFFSET)) {
		fwrite(ref->p, 2, 1, f);
		ref->p += 2;
	}
}

/*
 * Copies the variant from the reference binary if available, else uses
 * variant[0].
 */
static void
emit_variant(FILE *f, int tokenType, RefBin *ref)
{
	uint16_t v;
	if (ref && ref->p + 2 <= ref->end &&
	        ScriptTokenizer_MatchToken(ref->p, tokenType)) {
		memcpy(&v, ref->p, 2);
		ref->p += 2;
	} else {
		v = g_TokenVariants[tokenType][0];
	}
	fwrite(&v, 2, 1, f);
}

static void
emit_id(FILE *f, CScriptStringDB *db, const char *name, RefBin *ref)
{
	uint16_t v;
	int idx;
	uint16_t sdbidx;

	if (ref && ref->p + 4 <= ref->end &&
	        ScriptTokenizer_MatchToken(ref->p, T_ID)) {
		memcpy(&v, ref->p, 2);
		ref->p += 4;
	} else {
		v = g_TokenVariants[T_ID][0];
	}

	idx = CScriptStringDB_Find(db, name);
	if (idx < 0) {
		idx = CScriptStringDB_Add(db, name);
		if (idx < 0) {
			fprintf(stderr,
			        "wombat: warning: cannot add SDB string: %s\n",
			        name);
			idx = 0;
		}
	}
	sdbidx = (uint16_t)idx;
	fwrite(&v, 2, 1, f);
	fwrite(&sdbidx, 2, 1, f);
}

/*
 * Write a T_STR token chain for a string literal.
 *
 * The source string is authoritative for the SDB index - same model as
 * emit_id. When a reference binary is available we copy the variant byte
 * from it for byte-identical re-encoding, but the SDB index always comes
 * from looking up the source string. If the reference's first T_STR
 * happens to point at the same string, we copy the rest of the chain
 * verbatim too; otherwise we advance the reference past exactly one
 * T_STR (so subsequent tokens stay aligned) and emit a fresh T_STR for
 * the source string.
 */
static void
emit_str(FILE *f, CScriptStringDB *db, const char *str, RefBin *ref)
{
	uint16_t v;
	int idx;
	uint16_t sdbidx;
	int slen = strlen(str);
	int ref_matches = 0;

	if (ref && ref->p + 4 <= ref->end &&
	        ScriptTokenizer_MatchToken(ref->p, T_STR)) {
		uint16_t ridx;
		const char *rs;
		int rlen;
		memcpy(&ridx, ref->p + 2, 2);
		rs = CScriptStringDB_Get(db, ridx);
		if (rs != NULL) {
			rlen = strlen(rs);
			if (rlen >= 2 && rlen - 2 == slen &&
			        strncmp(str, rs + 1, slen) == 0)
				ref_matches = 1;
		}
		if (ref_matches) {
			/* Match: copy the whole chain verbatim. */
			while (ref->p + 4 <= ref->end &&
			        ScriptTokenizer_MatchToken(ref->p, T_STR)) {
				fwrite(ref->p, 4, 1, f);
				ref->p += 4;
			}
			return;
		}
		/* Mismatch: copy the first T_STR's variant for re-encoding,
		 * advance ref past the ENTIRE chain (so subsequent emits
		 * don't see dangling T_STR tokens from the mismatched
		 * reference structure), then fall through to emit a fresh
		 * T_STR for the source string. */
		memcpy(&v, ref->p, 2);
		while (ref->p + 4 <= ref->end &&
		        ScriptTokenizer_MatchToken(ref->p, T_STR)) {
			ref->p += 4;
		}
	} else {
		v = g_TokenVariants[T_STR][0];
	}

	/*
	 * Find an SDB entry where the inner content matches str.
	 * SDB entries are stored with surrounding quotes (e.g. "teleport").
	 * The binary's T_STR handler does str + 1 to skip the first char.
	 */
	for (idx = 0; idx < db->count; idx++) {
		int elen;
		if (db->strings[idx] == NULL)
			continue;
		elen = strlen(db->strings[idx]);
		if (elen < 2)
			continue;
		if (elen - 2 == slen &&
		        strncmp(str, db->strings[idx] + 1, slen) == 0) {
			sdbidx = (uint16_t)idx;
			fwrite(&v, 2, 1, f);
			fwrite(&sdbidx, 2, 1, f);
			return;
		}
	}

	/* String not found as a whole - add a new quoted SDB entry. */
	{
		char *quoted = malloc(slen + 3);
		if (quoted == NULL) {
			fprintf(stderr, "wombat: cannot allocate SDB string\n");
			return;
		}
		quoted[0] = '"';
		memcpy(quoted + 1, str, slen);
		quoted[slen + 1] = '"';
		quoted[slen + 2] = '\0';
		idx = CScriptStringDB_Add(db, quoted);
		free(quoted);
		if (idx < 0) {
			fprintf(stderr,
			        "wombat: cannot add SDB string: \"%s\"\n", str);
			return;
		}
		sdbidx = (uint16_t)idx;
		fwrite(&v, 2, 1, f);
		fwrite(&sdbidx, 2, 1, f);
	}
}

/*
 * Write an inline integer.
 * With a reference binary, copies the variant and encoding size from it.
 * Without a reference, uses the hex digit count from the source text to
 * preserve encoding size (0x%02X-> T_BYTE, 0x%04X-> T_WORD,
 * 0x%08X-> T_DWORD). For decimal numbers (hexdigits=0), uses the
 * smallest encoding that fits.
 */
static void
emit_int(FILE *f, uint32_t val, int hexdigits, RefBin *ref)
{
	/* With reference: copy variant and encoding size from it */
	if (ref && ref->p + 2 <= ref->end) {
		if (ScriptTokenizer_MatchToken(ref->p, T_BYTE) &&
		        ref->p + 3 <= ref->end) {
			uint16_t rv;
			unsigned char b = (unsigned char)val;
			memcpy(&rv, ref->p, 2);
			ref->p += 3;
			fwrite(&rv, 2, 1, f);
			fwrite(&b, 1, 1, f);
			return;
		}
		if (ScriptTokenizer_MatchToken(ref->p, T_WORD) &&
		        ref->p + 4 <= ref->end) {
			uint16_t rv;
			uint16_t w = (uint16_t)val;
			memcpy(&rv, ref->p, 2);
			ref->p += 4;
			fwrite(&rv, 2, 1, f);
			fwrite(&w, 2, 1, f);
			return;
		}
		if (ScriptTokenizer_MatchToken(ref->p, T_DWORD) &&
		        ref->p + 6 <= ref->end) {
			uint16_t rv;
			memcpy(&rv, ref->p, 2);
			ref->p += 6;
			fwrite(&rv, 2, 1, f);
			fwrite(&val, 4, 1, f);
			return;
		}
	}

	/* Without reference: use hex digit count or smallest fit */
	if (hexdigits >= 8 || val > 0xFFFF) {
		uint16_t v = g_TokenVariants[T_DWORD][0];
		fwrite(&v, 2, 1, f);
		fwrite(&val, 4, 1, f);
	} else if (hexdigits >= 4 || val > 0xFF) {
		uint16_t v = g_TokenVariants[T_WORD][0];
		uint16_t w = (uint16_t)val;
		fwrite(&v, 2, 1, f);
		fwrite(&w, 2, 1, f);
	} else {
		uint16_t v = g_TokenVariants[T_BYTE][0];
		unsigned char b = (unsigned char)val;
		fwrite(&v, 2, 1, f);
		fwrite(&b, 1, 1, f);
	}
}

/*
 * Simple tokenizer for Wombat source text
 */
typedef struct Lexer Lexer;
struct Lexer {
	const char *p;
	const char *end;
	char tok[4096]; /* current token text */
};

static void
skip_ws(Lexer *l)
{
	while (l->p < l->end && isspace((unsigned char)*l->p))
		l->p++;
}

/*
 * Returns 1 if a token was read, 0 at EOF.
 */
static int
lex_next(Lexer *l)
{
	int i;

	skip_ws(l);
	if (l->p >= l->end)
		return 0;

	i = 0;

	if (*l->p == '"') {
		l->p++; /* skip opening quote */
		while (l->p < l->end && *l->p != '"') {
			if (*l->p == '\\' && l->p + 1 < l->end) {
				l->p++; /* skip backslash */
				if (i < (int)sizeof(l->tok) - 1)
					l->tok[i++] = *l->p;
				l->p++;
			} else {
				if (i < (int)sizeof(l->tok) - 1)
					l->tok[i++] = *l->p;
				l->p++;
			}
		}
		if (l->p < l->end)
			l->p++; /* skip closing quote */
		l->tok[i] = '\0';
		/* Mark as string by prepending a special flag */
		memmove(l->tok + 1, l->tok, i + 1);
		l->tok[0] = '"';
		return 1;
	}

	if (l->p + 1 < l->end) {
		char c0, c1;
		c0 = l->p[0];
		c1 = l->p[1];
		if ((c0 == '=' && c1 == '=') || (c0 == '!' && c1 == '=') ||
		        (c0 == '<' && c1 == '=') || (c0 == '>' && c1 == '=') ||
		        (c0 == '&' && c1 == '&') || (c0 == '|' && c1 == '|') ||
		        (c0 == '+' && c1 == '+') || (c0 == '-' && c1 == '-')) {
			l->tok[0] = c0;
			l->tok[1] = c1;
			l->tok[2] = '\0';
			l->p += 2;
			return 1;
		}
	}

	if (strchr("(){}[];,=+-*/%<>!^", *l->p) != NULL) {
		l->tok[0] = *l->p;
		l->tok[1] = '\0';
		l->p++;
		return 1;
	}

	/* Hex literal: 0xNNNN */
	if (l->p + 1 < l->end && l->p[0] == '0' &&
	        (l->p[1] == 'x' || l->p[1] == 'X')) {
		l->tok[i++] = *l->p++;
		l->tok[i++] = *l->p++;
		while (l->p < l->end && isxdigit((unsigned char)*l->p)) {
			if (i < (int)sizeof(l->tok) - 1)
				l->tok[i++] = *l->p;
			l->p++;
		}
		l->tok[i] = '\0';
		return 1;
	}

	if (isdigit((unsigned char)*l->p)) {
		while (l->p < l->end && isdigit((unsigned char)*l->p)) {
			if (i < (int)sizeof(l->tok) - 1)
				l->tok[i++] = *l->p;
			l->p++;
		}
		l->tok[i] = '\0';
		return 1;
	}

	if (isalpha((unsigned char)*l->p) || *l->p == '_') {
		while (l->p < l->end &&
		        (isalnum((unsigned char)*l->p) || *l->p == '_')) {
			if (i < (int)sizeof(l->tok) - 1)
				l->tok[i++] = *l->p;
			l->p++;
		}
		l->tok[i] = '\0';
		return 1;
	}

	/* Unknown character - skip */
	l->tok[0] = *l->p;
	l->tok[1] = '\0';
	l->p++;
	return 1;
}

static int
text_to_tokentype(const char *text)
{
	int i;

	for (i = 0; g_TokenText[i].text != NULL; i++) {
		if (strcmp(g_TokenText[i].text, text) == 0)
			return g_TokenText[i].id;
	}
	return -1;
}

static int
event_name_to_trigger(const char *name)
{
	int i;

	for (i = 0; i < (int)nelem(g_TriggerEventNames); i++) {
		if (strcmp(g_TriggerEventNames[i], name) == 0)
			return i + 0x42;
	}
	return -1;
}

/*
 * wombat_encode - Tool-only. No binary equivalent.
 *
 * Encodes human-readable Wombat source back to compiled bytecode. If
 * refpath is non-NULL, loads the original binary and copies variant
 * encodings, integer sizes, T_OFFSET tokens, and T_STR chain structure
 * from it to produce a byte-identical round-trip - otherwise picks
 * variant[0] for each token, which still parses but won't match the
 * original byte stream.
 */
int
wombat_encode(const char *inpath, const char *outpath, CScriptStringDB *db,
        const char *refpath)
{
	FILE *fin, *fout;
	char *data;
	long len;
	Lexer lex;
	RefBin refbin;
	RefBin *ref;

	fin = fopen(inpath, "r");
	if (fin == NULL) {
		fprintf(stderr, "wombat: cannot open %s\n", inpath);
		return 1;
	}

	fseek(fin, 0, SEEK_END);
	len = ftell(fin);
	fseek(fin, 0, SEEK_SET);
	if (len < 0) {
		fclose(fin);
		fprintf(stderr, "wombat: cannot size %s\n", inpath);
		return 1;
	}

	data = malloc((size_t)len + 1);
	if (data == NULL) {
		fclose(fin);
		return 1;
	}
	if (fread(data, 1, (size_t)len, fin) != (size_t)len) {
		free(data);
		fclose(fin);
		fprintf(stderr, "wombat: short read on %s\n", inpath);
		return 1;
	}
	data[len] = '\0';
	fclose(fin);

	/* Load reference binary if provided */
	ref = NULL;
	memset(&refbin, 0, sizeof(refbin));
	if (refpath != NULL) {
		FILE *fref;
		fref = fopen(refpath, "rb");
		if (fref != NULL) {
			long rlen;
			char *rdata;
			fseek(fref, 0, SEEK_END);
			rlen = ftell(fref);
			fseek(fref, 0, SEEK_SET);
			if (rlen >= 0) {
				rdata = malloc((size_t)rlen);
				if (rdata != NULL &&
				        fread(rdata, 1, (size_t)rlen, fref) ==
				                (size_t)rlen) {
					refbin.data = rdata;
					refbin.end = rdata + rlen;
					refbin.p = rdata;
					ref = &refbin;
				} else {
					free(rdata);
				}
			}
			fclose(fref);
		}
	}

	fout = fopen(outpath, "wb");
	if (fout == NULL) {
		free(data);
		free((char *)refbin.data);
		fprintf(stderr, "wombat: cannot create %s\n", outpath);
		return 1;
	}

	lex.p = data;
	lex.end = data + len;

	while (lex_next(&lex)) {
		int toktype;
		int trigtype;

		ref_copy_offsets(ref, fout);

		/* String literal (marked with leading '"') */
		if (lex.tok[0] == '"') {
			emit_str(fout, db, lex.tok + 1, ref);
			continue;
		}

		if (isdigit((unsigned char)lex.tok[0])) {
			uint32_t val;
			int hexdigits;
			hexdigits = 0;
			if (lex.tok[0] == '0' &&
			        (lex.tok[1] == 'x' || lex.tok[1] == 'X')) {
				val = (uint32_t)strtoul(lex.tok, NULL, 16);
				hexdigits = strlen(lex.tok + 2);
			} else {
				val = (uint32_t)strtoul(lex.tok, NULL, 10);
			}
			emit_int(fout, val, hexdigits, ref);
			continue;
		}

		/* Punctuation / operators / keywords.
		 * When a reference binary is available, check whether
		 * it has T_ID at this position AND the SDB entry matches
		 * the token text - if so, treat it as an identifier even
		 * if it matches a keyword (e.g. "default" used as a
		 * variable name).  The SDB check prevents misaligned
		 * references (wrong script) from turning keywords into
		 * identifiers. */
		toktype = text_to_tokentype(lex.tok);
		if (toktype >= 0) {
			int use_id = 0;
			if (ref && ref->p + 4 <= ref->end &&
			        ScriptTokenizer_MatchToken(ref->p, T_ID)) {
				uint16_t ridx;
				const char *rs;
				memcpy(&ridx, ref->p + 2, 2);
				rs = CScriptStringDB_Get(db, ridx);
				if (rs != NULL && strcmp(rs, lex.tok) == 0)
					use_id = 1;
			}
			if (use_id) {
				emit_id(fout, db, lex.tok, ref);
			} else {
				emit_variant(fout, toktype, ref);
			}
			continue;
		}

		/* Trigger event name (lowercase)-> T_ID + SDB index */
		trigtype = event_name_to_trigger(lex.tok);
		if (trigtype >= 0) {
			emit_id(fout, db, lex.tok, ref);
			continue;
		}

		emit_id(fout, db, lex.tok, ref);
	}

	/* Copy any trailing T_OFFSET tokens from reference */
	ref_copy_offsets(ref, fout);

	fclose(fout);
	free(data);
	free((char *)refbin.data);
	return 0;
}
