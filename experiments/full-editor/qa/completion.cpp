#include "study.hpp"
#include "Scintilla.h"
#include "SciLexer.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <iterator>
namespace np4qa {
intptr_t dispatch(unsigned,uintptr_t,intptr_t);
namespace {
using std::min;
using UINT=unsigned; using LPCSTR=const char*; using LPSTR=char*; using LPARAM=intptr_t;
template<class T,class P> T AsInteger(P p) { return reinterpret_cast<T>(p); }
enum class CharacterClass { space,newLine,punctuation,word,cjkWord };
#define SciCall(m,w,l) dispatch((m),(w),(l))
#define __cdecl
#define NP2HeapAlloc(n) std::calloc(1,(n))
#define NP2HeapFree(p) std::free(p)
#define NP2_align_up(n,a) (((n)+(a)-1)&~((a)-1))
#define NP2_assume(c) ((void)0)
#define COUNTOF(a) std::size(a)
#define _strnicmp strncasecmp
bool StrNotEmpty(LPCSTR s) { return s && *s; }
uint32_t loadle_u32(const void *p) { uint32_t u; std::memcpy(&u,p,4); return u; }
uint32_t bit_zero_high_u32(uint32_t u,unsigned bits) {return u & ((1u<<bits)-1);}
uint32_t bswap32(uint32_t u) {return __builtin_bswap32(u);}
bool BitTestEx(const uint32_t (&m)[8],unsigned i) {return i<256 && ((m[i/32]>>(i%32))&1);}
bool IsASpaceOrTab(int c){return c==' '||c=='\t';}
constexpr bool IsAlpha(int c){return (unsigned((c|32)-'a')<26);}
int UnsafeLower(int c){return c|32;}
enum {LexerAttr_PrintfFormatSpecifier=1, LexerAttr_CppPreprocessor=2, LexerAttr_EscapePunctuation=4, LexerAttr_PlainTextFile=8};
struct { int iLexer=SCLEX_JSON, lexerAttr=0, formatSpecifierStyle=0, escapeCharacterStart='\\', escapeCharacterStyle=SCE_JSON_ESCAPECHAR, operatorStyle=SCE_JSON_OPERATOR,operatorStyle2=0; } json;
auto pLexCurrent=&json;
enum {AutoCompleteScope_Commont=1,AutoCompleteScope_String=2,AutoCompleteScope_PlainText=4,
    AutoCompletionOption_ScanWordsInDocument=4,AutoCompletionOption_OnlyWordsInDocument=8,
    AutoCompleteCondition_Normal=0,AutoCompleteCondition_OnCharAdded=1,AutoCompleteCondition_OnCharDeleted=2};
struct EditAutoCompletionConfig {
    unsigned dwScanWordsTimeout=150, iVisibleItemCount=10,iPreviousItemCount=0;
    int iMinWordLength=2,iMinNumberLength=0,iCompleteOption=6,fCompleteScope=255,fScanWordScope=255;
    bool bIgnoreCase=true;
    char szAutoCompleteFillUp[1]={0};
};
EditAutoCompletionConfig autoCompletionConfig;
using Clock=std::chrono::steady_clock;
using HANDLE=Clock::time_point*;
Clock::time_point deadline;
HANDLE idleTaskTimer=&deadline;
void WaitableTimer_Set(HANDLE t,unsigned ms){*t=Clock::now()+std::chrono::milliseconds(ms);}
bool WaitableTimer_Continue(HANDLE t){return Clock::now() < *t;}
#include "completion_slice.inc"
}
void configureCompletion() {
    std::copy_n(DefaultWordCharSet,8,CurrentWordCharSet);
    autoCompletionConfig.iPreviousItemCount=0;
    dispatch(SCI_SETKEYWORDS,0,reinterpret_cast<intptr_t>(Keywords_JSON[0]));
}
void complete() { EditCompleteWord(AutoCompleteCondition_Normal,false); }
void typed(unsigned ch,int source,int handled) {
    // The word-completion branch of Notepad4.cpp's SCN_CHARADDED handler.
    if (!handled && !(source != SC_CHARACTERSOURCE_DIRECT_INPUT && ch >= 0x80)
        && IsAutoCompletionWordCharacter(ch))
        EditCompleteWord(dispatch(SCI_AUTOCACTIVE,0,0) ? AutoCompleteCondition_OnCharAdded : AutoCompleteCondition_Normal,false);
}
}
