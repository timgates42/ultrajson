/*
Copyright (c) 2011, Jonas Tarnstrom and ESN Social Software AB
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. All advertising materials mentioning features or use of this software
   must display the following acknowledgement:
   This product includes software developed by ESN Social Software AB (www.esn.me).
4. Neither the name of the ESN Social Software AB nor the
   names of its contributors may be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY ESN SOCIAL SOFTWARE AB ''AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL ESN SOCIAL SOFTWARE AB BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Portions of code from:
MODP_ASCII - Ascii transformations (upper/lower, etc)
http://code.google.com/p/stringencoders/
Copyright (c) 2007  Nick Galbreath -- nickg [at] modp [dot] com. All rights reserved.

*/

#include "ultrajson.h"
#include <math.h>
#include <assert.h>
#include <string.h>

struct DecoderState
{
	char *start;
	char *end;
	char *escStart;
	char *escEnd;
	int escHeap;
	int lastType;
	JSONObjectDecoder *dec;
};

JSOBJ FASTCALL_MSVC decode_any( struct DecoderState *ds) FASTCALL_ATTR;

typedef JSOBJ (*PFN_DECODER)( struct DecoderState *ds);
PFN_DECODER g_identTable[256] = { NULL }; 

/*
FIXME: Maybe move this to inside of createDouble function. Might increase memory locality and worst case 
possibly polute the global namespace less */
static const double g_pow10[] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};


/*
#define RETURN_JSOBJ_NULLCHECK(_expr)\
{ \
	JSOBJ obj__ = (_expr); \
	if (obj__ == NULL) \
		fprintf (stderr, "Function at %s:%d returned NULL", __FUNCTION__, __LINE__); \
	return obj__; \
} \
*/

#define RETURN_JSOBJ_NULLCHECK(_expr) return(_expr);

double createDouble(double intNeg, double intValue, double frcValue, int frcDecimalCount)
{
	return (intValue + (frcValue / g_pow10[frcDecimalCount])) * intNeg;
}

static JSOBJ SetError( struct DecoderState *ds, int offset, const char *message)
{
	ds->dec->errorOffset = ds->start + offset;
	ds->dec->errorStr = (char *) message;
	return NULL;
}


FASTCALL_ATTR JSOBJ FASTCALL_MSVC decode_numeric ( struct DecoderState *ds)
{
#ifdef JSON_NUMERIC_PRECISION_JAVASCRIPT
	double intNeg = 1;
	double intValue;
#else
	int intNeg = 1;
	JSLONG intValue;
#endif

	double expNeg;
	int chr;
	int decimalCount = 0;
	double frcValue = 0.0;
	double expValue;

	if (*(ds->start) == '-')
	{
		ds->start ++;
		intNeg = -1;
	}

	// Scan integer part
	intValue = 0;

	while (1)
	{
		chr = (int) (unsigned char) *(ds->start);

		switch (chr)
		{
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			//FIXME: Check for arithemtic overflow here
			//PERF: Don't do 64-bit arithmetic here unless we know we have to
#ifdef JSON_NUMERIC_PRECISION_JAVASCRIPT
			intValue = intValue * 10.0 + (double) (chr - 48);
#else
			intValue = intValue * 10 + (JSLONG) (chr - 48);
#endif
			ds->start ++;
			break;

		case '.':
			ds->start ++;
			goto DECODE_FRACTION;
			break;

		case 'e':
		case 'E':
			ds->start ++;
			goto DECODE_EXPONENT;
			break;

		default:
			goto BREAK_INT_LOOP;
			break;
		}
	}

BREAK_INT_LOOP:

	ds->lastType = JT_INT;

	//If input string is LONGLONG_MIN here the value is already negative so we should not flip it

#ifdef JSON_NUMERIC_PRECISION_JAVASCRIPT
#else
	if (intValue < 0)
	{
		intNeg = 1;
	}
#endif

	//dbg1 = (intValue * intNeg);
	//dbg2 = (JSLONG) dbg1;

	//FIXME: Check value size here, don't decode everything as 64-bit

	RETURN_JSOBJ_NULLCHECK(ds->dec->newLong( (JSINT64) (intValue * intNeg)));

DECODE_FRACTION:

	// Scan fraction part
	frcValue = 0.0;
	while (1)
	{
		chr = (int) (unsigned char) *(ds->start);

		switch (chr)
		{
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			if (decimalCount < JSON_DOUBLE_MAX_DECIMALS)
			{
				frcValue = frcValue * 10.0 + (double) (chr - 48);
				decimalCount ++;
			}
			ds->start ++;
			break;

		case 'e':
		case 'E':
			ds->start ++;
			goto DECODE_EXPONENT;
			break;

		default:
			goto BREAK_FRC_LOOP;
		}
	}

BREAK_FRC_LOOP:

	if (intValue < 0)
	{
		intNeg = 1;
	}

	//FIXME: Check for arithemtic overflow here
	ds->lastType = JT_DOUBLE;
	RETURN_JSOBJ_NULLCHECK(ds->dec->newDouble (createDouble( (double) intNeg, (double) intValue, frcValue, decimalCount)));

DECODE_EXPONENT:
	expNeg = 1.0;

	if (*(ds->start) == '-')
	{
		expNeg = -1.0;
		ds->start ++;
	}
	else
	if (*(ds->start) == '+')
	{
		expNeg = +1.0;
		ds->start ++;
	}

	expValue = 0.0;

	while (1)
	{
		chr = (int) (unsigned char) *(ds->start);

		switch (chr)
		{
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			expValue = expValue * 10.0 + (double) (chr - 48);
			ds->start ++;
			break;

		default:
			goto BREAK_EXP_LOOP;

		}
	}

BREAK_EXP_LOOP:

#ifdef JSON_NUMERIC_PRECISION_JAVASCRIPT
#else
	if (intValue < 0)
	{
		intNeg = 1;
	}
#endif
	
	//FIXME: Check for arithemtic overflow here
	ds->lastType = JT_DOUBLE;
	RETURN_JSOBJ_NULLCHECK(ds->dec->newDouble (createDouble( (double) intNeg, (double) intValue , frcValue, decimalCount) * pow(10.0, expValue)));
}

FASTCALL_ATTR JSOBJ FASTCALL_MSVC decode_true ( struct DecoderState *ds) 
{
	ds->start ++;

	if (*(ds->start++) != 'r')
		goto SETERROR;
	if (*(ds->start++) != 'u')
		goto SETERROR;
	if (*(ds->start++) != 'e')
		goto SETERROR;

	ds->lastType = JT_TRUE;
	RETURN_JSOBJ_NULLCHECK(ds->dec->newTrue());

SETERROR:
	return SetError(ds, -1, "Unexpected character found when decoding 'true'");
}

FASTCALL_ATTR JSOBJ FASTCALL_MSVC decode_false ( struct DecoderState *ds) 
{
	ds->start ++;

	if (*(ds->start++) != 'a')
		goto SETERROR;
	if (*(ds->start++) != 'l')
		goto SETERROR;
	if (*(ds->start++) != 's')
		goto SETERROR;
	if (*(ds->start++) != 'e')
		goto SETERROR;

	ds->lastType = JT_FALSE;
	RETURN_JSOBJ_NULLCHECK(ds->dec->newFalse());

SETERROR:
	return SetError(ds, -1, "Unexpected character found when decoding 'false'");

}


FASTCALL_ATTR JSOBJ FASTCALL_MSVC decode_null ( struct DecoderState *ds) 
{
	ds->start ++;

	if (*(ds->start++) != 'u')
		goto SETERROR;
	if (*(ds->start++) != 'l')
		goto SETERROR;
	if (*(ds->start++) != 'l')
		goto SETERROR;

	ds->lastType = JT_NULL;
	RETURN_JSOBJ_NULLCHECK(ds->dec->newNull());

SETERROR:
	return SetError(ds, -1, "Unexpected character found when decoding 'null'");
}

FASTCALL_ATTR void FASTCALL_MSVC SkipWhitespace(struct DecoderState *ds) 
{
	while (1)
	{
		switch (*ds->start)
		{
		case ' ':
		case '\t':
		case '\r':
		case '\n':
			ds->start ++;
			break;

		default:
			return;
		}
	}
}

FASTCALL_ATTR JSOBJ FASTCALL_MSVC decode_string ( struct DecoderState *ds)
{
	char *escOffset;
	char chr;
	size_t escLen = (ds->escEnd - ds->escStart);
	ds->lastType = JT_INVALID;
	ds->start ++;

	if ( (ds->end - ds->start) > escLen)
	{
		size_t newSize = escLen * 2;

		if (ds->escHeap)
		{
			ds->escStart = (char *) ds->dec->realloc (ds->escStart, escLen);
		}
		else
		{
			char *oldStart = ds->escStart;
			ds->escHeap = 1;
			ds->escStart = (char *) ds->dec->malloc (newSize);
			memcpy (ds->escStart, oldStart, escLen);
		}

		ds->escEnd = ds->escStart + newSize;
	}

	escOffset = ds->escStart;

	while(1)
	{
		chr = (*ds->start++);

		switch (chr)
		{
		case '\0':
			return SetError(ds, -1, "Unmatched ''\"' when when decoding 'string'");

		case '\"':
			ds->lastType = JT_UTF8;
			RETURN_JSOBJ_NULLCHECK(ds->dec->newString(ds->escStart, escOffset));

		case '\\':
			switch (*(ds->start))
			{
			case '\\': *(escOffset++) = '\\'; ds->start++; continue;
			case '\"': *(escOffset++) = '\"'; ds->start++; continue;
			case '/':  *(escOffset++) = '/'; ds->start++; continue;
			case 'b':  *(escOffset++) = '\b'; ds->start++; continue;
			case 'f':  *(escOffset++) = '\f'; ds->start++; continue;
			case 'n':  *(escOffset++) = '\n'; ds->start++; continue;
			case 'r':  *(escOffset++) = '\r'; ds->start++; continue;
			case 't':  *(escOffset++) = '\t'; ds->start++; continue;

			case '\0': 
				return SetError(ds, -1, "Unterminated escape sequence when decoding 'string'");

			default:
				return SetError(ds, -1, "Unrecognized escape sequence when decoding 'string'");
			}
			break;

		default:
			*(escOffset++) = chr;
		}
	}

}

FASTCALL_ATTR JSOBJ FASTCALL_MSVC decode_array( struct DecoderState *ds)
{
	JSOBJ itemValue;
	JSOBJ newObj = ds->dec->newArray();

	ds->lastType = JT_INVALID;
	ds->start ++;

	while (1)//(*ds->start) != '\0')
	{
		SkipWhitespace(ds);

		if ((*ds->start) == ']')
		{
			*ds->start ++;
			return newObj;
		}

		itemValue = decode_any(ds);

		if (itemValue == NULL)
		{
			ds->dec->releaseObject(newObj);
			return NULL;
		}

		ds->dec->arrayAddItem (newObj, itemValue);

		SkipWhitespace(ds);

		switch (*(ds->start++))
		{
			case ']':
				return newObj;

			case ',':
				break;

			default:
				ds->dec->releaseObject(newObj);
				return SetError(ds, -1, "Unexpected character in found when decoding array value");
		}
	}

	ds->dec->releaseObject(newObj);
	return SetError(ds, -1, "Unmatched ']' when decoding 'array'");
}



FASTCALL_ATTR JSOBJ FASTCALL_MSVC decode_object( struct DecoderState *ds)
{
	JSOBJ itemName;
	JSOBJ itemValue;
	JSOBJ newObj = ds->dec->newObject();

	ds->start ++;

	while (1)
	{
		SkipWhitespace(ds);

		if ((*ds->start) == '}')
		{
			ds->start ++;
			return newObj;
		}

		ds->lastType = JT_INVALID;
		itemName = decode_any(ds);

		if (itemName == NULL)
		{
			ds->dec->releaseObject(newObj);
			return newObj;
		}

		if (ds->lastType != JT_UTF8)
		{
			ds->dec->releaseObject(newObj);
			ds->dec->releaseObject(itemName);
			return SetError(ds, -1, "Key name of object must be 'string' when decoding 'object'");
		}

		SkipWhitespace(ds);

		if (*(ds->start++) != ':')
		{
			ds->dec->releaseObject(newObj);
			ds->dec->releaseObject(itemName);
			return SetError(ds, -1, "No ':' found when decoding object value");
		}

		SkipWhitespace(ds);

		itemValue = decode_any(ds);

		if (itemValue == NULL)
		{
			ds->dec->releaseObject(newObj);
			ds->dec->releaseObject(itemName);
			return newObj;
		}

		ds->dec->objectAddKey (newObj, itemName, itemValue);

		SkipWhitespace(ds);

		switch (*(ds->start++))
		{
			case '}':
				return newObj;

			case ',':
				break;

			default:
				ds->dec->releaseObject(newObj);
				return SetError(ds, -1, "Unexpected character in found when decoding object value");
		}
	}

	ds->dec->releaseObject(newObj);
	return SetError(ds, -1, "Unmatched '}' when decoding object");
}

FASTCALL_ATTR JSOBJ FASTCALL_MSVC decode_any(struct DecoderState *ds)
{
	while (1)
	{
		switch (*ds->start)
		{
			case '\"': 
				return decode_string (ds);
			case '0': 
			case '1':
			case '2': 
			case '3': 
			case '4': 
			case '5':
			case '6': 
			case '7': 
			case '8': 
			case '9': 
			case '-': 
				return decode_numeric (ds);

			case '[':	return decode_array (ds);
			case '{': return decode_object (ds);
			case 't': return decode_true (ds);
			case 'f': return decode_false (ds);
			case 'n': return decode_null (ds);

			case ' ':
			case '\t':
			case '\r':
			case '\n':
				// White space
				ds->start ++;
				break;

			default:
				return SetError(ds, -1, "Expected object or value");
		}
	}
}


JSOBJ JSON_DecodeObject(JSONObjectDecoder *dec, const char *buffer, size_t cbBuffer)
{
	static int s_once = 1;

	struct DecoderState ds;
	int index;
	char escBuffer[65536];
	JSOBJ ret;
	

	ds.start = (char *) buffer;
	ds.end = ds.start + cbBuffer;
	ds.escStart = escBuffer;
	ds.escEnd = ds.escStart + sizeof(escBuffer);
	ds.escHeap = 0;
	ds.dec = dec;
	ds.dec->errorStr = NULL;
	ds.dec->errorOffset = NULL;

	ds.dec = dec;

	ret = decode_any (&ds);
	
	if (ds.escHeap)
	{
		dec->free(ds.escStart);
	}
	return ret;
}
