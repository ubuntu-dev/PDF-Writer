#include "PDFObjectParser.h"
#include "PDFObject.h"
#include "PDFBoolean.h"
#include "PDFLiteralString.h"
#include "PDFHexString.h"
#include "PDFNull.h"
#include "PDFName.h"
#include "PDFReal.h"
#include "PDFInteger.h"
#include "PDFArray.h"
#include "PDFDictionary.h"
#include "PDFIndirectObjectReference.h"
#include "PDFSymbol.h"
#include "PDFStreamInput.h"
#include "Trace.h"
#include "BoxingBase.h"
#include "PDFStream.h"
#include "IByteReaderWithPosition.h"
#include "RefCountPtr.h"
#include "PDFObjectCast.h"

#include <sstream>

PDFObjectParser::PDFObjectParser(void)
{
	ResetReadState();
}

PDFObjectParser::~PDFObjectParser(void)
{
}

void PDFObjectParser::SetReadStream(IByteReaderWithPosition* inSourceStream)
{
	mStream = inSourceStream;
	mTokenizer.SetReadStream(inSourceStream);
	ResetReadState();
}

void PDFObjectParser::ResetReadState()
{
	mTokenBuffer.clear();
	mTokenizer.ResetReadState();
}

/*
4. Stream (including the dictionary)
*/

static const string scR = "R";
static const string scStream = "stream";
PDFObject* PDFObjectParser::ParseNewObject()
{
	PDFObject* pdfObject = NULL;
	string token;

	do
	{
		if(!GetNextToken(token))
			break;

		// based on the parsed token, and parhaps some more, determine the type of object
		// and how to parse it.

		// Boolean
		if(IsBoolean(token))
		{
			pdfObject = ParseBoolean(token);
			break;
		} 
		// Literal String
		else if(IsLiteralString(token))
		{
			pdfObject = ParseLiteralString(token);
			break;
		}
		// Hexadecimal String
		else if(IsHexadecimalString(token))
		{
			pdfObject = ParseHexadecimalString(token);
			break;
		}
		// NULL
		else if (IsNull(token))
		{
			pdfObject = new PDFNull();
			break;
		}
		// Name
		else if(IsName(token))
		{
			pdfObject = ParseName(token);
			break;
		}
		// Number (and possibly an indirect reference)
		else if(IsNumber(token))
		{	
			pdfObject = ParseNumber(token);
			
			// this could be an indirect reference in case this is a positive integer
			// and the next one is also, and then there's an "R" keyword
			if(pdfObject && 
				(pdfObject->GetType() == ePDFObjectInteger) && 
				((PDFInteger*)pdfObject)->GetValue() > 0)
			{
				// try parse version
				string numberToken;
				if(!GetNextToken(numberToken)) // k. no next token...cant be reference
					break;

				if(!IsNumber(numberToken)) // k. no number, cant be reference
				{
					SaveTokenToBuffer(numberToken);
					break;
				}

				PDFObject* versionObject = ParseNumber(numberToken); 
				if(!versionObject || 
					(versionObject->GetType() != ePDFObjectInteger) ||
					((PDFInteger*)versionObject)->GetValue() < 0) // k. failure to parse number, or no non-negative, cant be reference
				{
					SaveTokenToBuffer(numberToken);
					break;
				}

				// try parse R keyword
				string keywordToken;
				if(!GetNextToken(keywordToken)) // k. no next token...cant be reference
					break;

				if(keywordToken != scR) // k. not R...cant be reference
				{
					SaveTokenToBuffer(numberToken);
					SaveTokenToBuffer(keywordToken);
					break;
				}

				// if passed all these, then this is a reference
				PDFObject* referenceObject = new PDFIndirectObjectReference(
													((PDFInteger*)pdfObject)->GetValue(),
													((PDFInteger*)versionObject)->GetValue());
				delete pdfObject;
				delete versionObject;
				pdfObject = referenceObject;
			}
			break;
		}
		// Array
		else if(IsArray(token))
		{
			pdfObject = ParseArray();
			break;
		}
		// Dictionary
		else if (IsDictionary(token))
		{
			pdfObject = ParseDictionary();

			if(pdfObject)
			{
				// could be a stream. will be if the next token is the "stream" keyword
				if(!GetNextToken(token))
					break;

				if(scStream == token) 
				{
					// yes, found a stream. record current position as the position where the stream starts. 
					// [tokenizer took care that the posiiton should be that way with a special case]
					pdfObject = new PDFStreamInput((PDFDictionary*)pdfObject,mStream->GetCurrentPosition());
				}
				else
				{
					SaveTokenToBuffer(token);
				}
			}

			break;
		}
		// Symbol (legitimate keyword or error. determine if error based on semantics)
		else
			pdfObject = new PDFSymbol(token);
	}while(false);


	return pdfObject;

}

bool PDFObjectParser::GetNextToken(string& outToken)
{
	if(mTokenBuffer.size() > 0)
	{
		outToken = mTokenBuffer.front();
		mTokenBuffer.pop_front();
		return true;
	}
	else
	{
		// skip comments
		BoolAndString tokenizerResult;

		do
		{
			tokenizerResult = mTokenizer.GetNextToken();
			if(tokenizerResult.first && !IsComment(tokenizerResult.second))
			{
				outToken = tokenizerResult.second;
				break;
			}
		}while(tokenizerResult.first);
		return tokenizerResult.first;
	}
}

static const string scTrue = "true";
static const string scFalse = "false";
bool PDFObjectParser::IsBoolean(const string& inToken)
{
	return (scTrue == inToken || scFalse == inToken);	
}

PDFObject* PDFObjectParser::ParseBoolean(const string& inToken)
{
	return new PDFBoolean(scTrue == inToken);
}

static const char scLeftParanthesis = '(';
bool PDFObjectParser::IsLiteralString(const string& inToken)
{
	return inToken.at(0) == scLeftParanthesis;
}

static const char scRightParanthesis = ')';
PDFObject* PDFObjectParser::ParseLiteralString(const string& inToken)
{
	stringbuf stringBuffer;
	Byte buffer;
	string::const_iterator it = inToken.begin();
	size_t i=1;
	++it; // skip first paranthesis
	
	// verify that last character is ')'
	if(inToken.at(inToken.size()-1) != scRightParanthesis)
	{
		TRACE_LOG1("PDFObjectParser::ParseLiteralString, exception in parsing literal string, no closing paranthesis, Expression: %s",inToken.c_str());
		return NULL;
	}

	for(; i < inToken.size()-1;++it,++i)
	{
		if(*it == '\\')
		{
			++it;
			if('0' <= *it && *it <= '7')
			{
				buffer = (*it - '0') * 64;
				++it;
				buffer += (*it - '0') * 8;
				++it;
				buffer += (*it - '0');
			}
			else
			{
				switch(*it)
				{
					case 'n':
						buffer = '\n';
						break;
					case 'r':
						buffer = '\r';
						break;
					case 't':
						buffer = '\t';
						break;
					case 'b':
						buffer = '\b';
						break;
					case 'f':
						buffer = '\f';
						break;
					case '\\':
						buffer = '\\';
						break;
					case '(':
						buffer = '(';
						break;
					case ')':
						buffer = ')';
						break;
					default:
						// error!
						buffer = 0;
						break;
				}
			}
		}
		else
		{
			buffer = *it;
		}
		stringBuffer.sputn((const char*)&buffer,1);
	}
	return new PDFLiteralString(stringBuffer.str());
}

static const char scLeftAngle = '<';
bool PDFObjectParser::IsHexadecimalString(const string& inToken)
{
	// first char should be left angle brackets, and the one next must not (otherwise it's a dictionary start)
	return (inToken.at(0) == scLeftAngle) && (inToken.size() < 2 || inToken.at(1) != scLeftAngle);
}


static const char scRightAngle = '>';
PDFObject* PDFObjectParser::ParseHexadecimalString(const string& inToken)
{
	EStatusCode status = eSuccess;
	
	// verify that last character is '>'
	if(inToken.at(inToken.size()-1) != scRightAngle)
	{
		TRACE_LOG1("PDFObjectParser::ParseHexadecimalString, exception in parsing hexadecimal string, no closing angle, Expression: %s",inToken.c_str());
		return NULL;
	}

	return new PDFHexString(inToken.substr(1,inToken.size()-2));
}

static const string scNull = "null";
bool PDFObjectParser::IsNull(const string& inToken)
{
	return scNull == inToken;
}

static const char scSlash = '/';
bool PDFObjectParser::IsName(const string& inToken)
{
	return inToken.at(0) == scSlash;
}

static const char scSharp = '#';
PDFObject* PDFObjectParser::ParseName(const string& inToken)
{
	EStatusCode status = eSuccess;
	stringbuf stringBuffer;
	BoolAndByte hexResult;
	Byte buffer;
	string::const_iterator it = inToken.begin();
	++it; // skip initial slash

	for(; it != inToken.end() && eSuccess == status; ++it)
	{
		if(*it == scSharp)
		{
			// hex representation
			++it;
			if(it == inToken.end())
			{
				TRACE_LOG1("PDFObjectParser::ParseName, exception in parsing hex value for a name token. token = %s",inToken.c_str());
				status = eFailure;
				break;
			}
			hexResult = GetHexValue(*it);
			if(!hexResult.first)
			{
				TRACE_LOG1("PDFObjectParser::ParseName, exception in parsing hex value for a name token. token = %s",inToken.c_str());
				status = eFailure;
				break;
			}
			buffer=(hexResult.second << 4);
			++it;
			if(it == inToken.end())
			{
				TRACE_LOG1("PDFObjectParser::ParseName, exception in parsing hex value for a name token. token = %s",inToken.c_str());
				status = eFailure;
				break;
			}
			hexResult = GetHexValue(*it);
			if(!hexResult.first)
			{
				TRACE_LOG1("PDFObjectParser::ParseName, exception in parsing hex value for a name token. token = %s",inToken.c_str());
				status = eFailure;
				break;
			}
			buffer+=hexResult.second;
		}
		else
		{
			buffer = *it;
		}
		stringBuffer.sputn((const char*)&buffer,1);
	}
	
	if(eSuccess == status)
		return new PDFName(stringBuffer.str());
	else
		return NULL;
}

static const char scPlus = '+';
static const char scMinus = '-';
static const char scNine = '9';
static const char scZero = '0';
static const char scDot = '.';
bool PDFObjectParser::IsNumber(const string& inToken)
{
	// it's a number if the first char is either a sign or digit, and the rest is 
	// digits, with the exception of a dot which can appear just once.

	if(inToken.at(0) != scPlus && inToken.at(0) != scMinus && (inToken.at(0) > scNine || inToken.at(0) < scZero))
		return false;

	bool isNumber = true;
	bool dotEncountered = false;
	string::const_iterator it = inToken.begin();
	++it; //verified the first char already

	// only sign is not a number
	if((inToken.at(0) == scPlus || inToken.at(0) == scMinus) && it == inToken.end())
		return false;

	for(; it != inToken.end() && isNumber;++it)
	{
		if(*it == scDot)
		{
			if(dotEncountered)
			{
				isNumber = false;
			}
			dotEncountered = true;
		}
		else
			isNumber = (scZero <= *it && *it <= scNine);
	}
	return isNumber;
}

PDFObject* PDFObjectParser::ParseNumber(const string& inToken)
{
	// once we know this is a number, then parsing is easy. just determine if it's a real or integer, so as to separate classes for better accuracy
	if(inToken.find(scDot) != inToken.npos)
		return new PDFReal(Double(inToken));
	else
		return new PDFInteger(Long(inToken));
}

static const string scLeftSquare = "[";
bool PDFObjectParser::IsArray(const string& inToken)
{
	return scLeftSquare == inToken;
}

static const string scRightSquare = "]";
PDFObject* PDFObjectParser::ParseArray()
{
	PDFArray* anArray = new PDFArray();
	bool arrayEndEncountered = false;
	string token;
	EStatusCode status = eSuccess;

	// easy one. just loop till you get to a closing bracket token and recurse
	while(GetNextToken(token) && eSuccess == status)
	{
		arrayEndEncountered = (scRightSquare == token);
		if(arrayEndEncountered)
			break;

		ReturnTokenToBuffer(token);
		RefCountPtr<PDFObject> anObject(ParseNewObject());
		if(!anObject)
		{
			status = eFailure;
			TRACE_LOG1("PDFObjectParser::ParseArray, failure to parse array, failed to parse a member object. token = %s",token);
		}
		else
		{
			anArray->AppendObject(anObject.GetPtr());
		}
	}

	if(arrayEndEncountered && eSuccess == status)
	{
		return anArray;
	}
	else
	{
		delete anArray;
		TRACE_LOG1("PDFObjectParser::ParseArray, failure to parse array, didn't find end of array or failure to parse array member object. token = %s",token);
		return NULL;
	}
}

void PDFObjectParser::SaveTokenToBuffer(string& inToken)
{
	mTokenBuffer.push_back(inToken);
}

void PDFObjectParser::ReturnTokenToBuffer(string& inToken)
{
	mTokenBuffer.push_front(inToken);
}

static const string scDoubleLeftAngle = "<<";
bool PDFObjectParser::IsDictionary(const string& inToken)
{
	return scDoubleLeftAngle == inToken;
}

static const string scDoubleRightAngle = ">>";
PDFObject* PDFObjectParser::ParseDictionary()
{
	PDFDictionary* aDictionary = new PDFDictionary();
	PDFObject* aKey = NULL;
	PDFObject* aValue = NULL;
	bool dictionaryEndEncountered = false;
	string token;
	EStatusCode status = eSuccess;

	while(GetNextToken(token) && eSuccess == status)
	{
		dictionaryEndEncountered = (scDoubleRightAngle == token);
		if(dictionaryEndEncountered)
			break;

		ReturnTokenToBuffer(token);

		// Parse Key
		PDFObjectCastPtr<PDFName> aKey(ParseNewObject());
		if(!aKey)
		{
			status = eFailure;
			TRACE_LOG1("PDFObjectParser::ParseDictionary, failure to parse key for a dictionary. token = %s",token);
			break;
		}

		// i'll consider duplicate keys as failure
		if(aDictionary->Exists(aKey->GetValue()))
		{
			status = eFailure;
			TRACE_LOG1("PDFObjectParser::ParseDictionary, failure to parse key for a dictionary, key already exists. key = %s",aKey->GetValue().c_str());
			break;
		}

		// Parse Value
		RefCountPtr<PDFObject> aValue = ParseNewObject();
		if(!aValue)
		{
			status = eFailure;
			TRACE_LOG1("PDFObjectParser::ParseDictionary, failure to parse value for a dictionary. token = %s",token);
			break;
		}
	
		// all well, add the two items to the dictionary
		aDictionary->Insert(aKey.GetPtr(),aValue.GetPtr());
	}

	if(dictionaryEndEncountered && eSuccess == status)
	{
		return aDictionary;
	}
	else
	{
		delete aDictionary;
		TRACE_LOG1("PDFObjectParser::ParseDictionary, failure to parse dictionary, didn't find end of array or failure to parse dictionary member object. token = %s",token);
		return NULL;
	}
}

static const char scCommentStart = '%';
bool PDFObjectParser::IsComment(const string& inToken)
{
	return inToken.at(0) == scCommentStart;
}

BoolAndByte PDFObjectParser::GetHexValue(Byte inValue)
{
	if('0' <= inValue && inValue <='9')
		return BoolAndByte(true,inValue - '0');
	else if('A' <= inValue && inValue <= 'F')
		return BoolAndByte(true,inValue - 'A');
	else if('a' <= inValue && inValue <= 'f')
		return BoolAndByte(true,inValue - 'a');
	else
	{
		TRACE_LOG1("PDFObjectParser::GetHexValue, unrecongnized hex value - %c",inValue);
		return BoolAndByte(false,inValue);
	}
}