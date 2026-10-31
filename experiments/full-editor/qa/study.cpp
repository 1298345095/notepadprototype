#include "study.hpp"
#include "Scintilla.h"
#include "SciLexer.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <pango/pangocairo.h>

namespace np4qa {
void configureCompletion();
void typed(unsigned,int,int);
namespace fs = std::filesystem;
namespace {
Send send;
fs::path exchange;
std::string fixture, candidates;
const std::string marker = "AéB中😀Z";
const std::string alpha = "np4ProofCandidateAlpha", beta = "np4ProofCandidateBeta", prefix = "np4ProofCan";
int lastSequence = -1, accepts = 0, tentative = 0, committed = 0;
bool loading = false;
intptr_t dragStart = -1, dragEnd = -1;
intptr_t call(unsigned m, uintptr_t w=0, intptr_t l=0) { return send(m,w,l); }
intptr_t str(const char *s) {return reinterpret_cast<intptr_t>(s);}
std::string encode(const std::string &s) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out; for (unsigned char c:s) {out+=digits[c>>4];out+=digits[c&15];} return out;
}
std::string readFile(const char *path) {
    std::ifstream input(path,std::ios::binary);
    if (!input) { std::fprintf(stderr,"Cannot open UTF-8 document: %s\n",path); std::exit(2); }
    std::ostringstream bytes; bytes<<input.rdbuf(); return bytes.str();
}
std::string text() {
    std::string bytes(call(SCI_GETLENGTH)+1,'\0');
    call(SCI_GETTEXT,bytes.size(),reinterpret_cast<intptr_t>(bytes.data()));
    bytes.pop_back(); return bytes;
}
std::string sample() {
    std::string s="{\n  \"foldMe\": [\n    17,\n    19\n  ],\n  \"long\": \"";
    for(int i=0;i<7;i++) {
        for(int k=0;k<9;k++) s+="wrap sample ";
        s+=marker+" ";
    }
    return s+"\",\n  \""+alpha+"\": 1,\n  \""+beta+"\": 2\n}\n";
}
// Use a separate Pango layout. Calibrate the pixel size by the actual width of
// eight ASCII glyphs, then shape each whole prefix, including combining marks.
int advance(const std::string &prefixText) {
    PangoContext *context=pango_font_map_create_context(pango_cairo_font_map_get_default());
    auto *layout=pango_layout_new(context);
    auto *font=pango_font_description_from_string("DejaVu Sans Mono");
    const auto reference=call(SCI_TEXTWIDTH,STYLE_DEFAULT,str("MMMMMMMM"));
    auto width=[&](const char *s) { int w=0; pango_layout_set_text(layout,s,-1);pango_layout_get_size(layout,&w,nullptr);return double(w)/PANGO_SCALE; };
    double best=1000, size=16;
    for(int eighths=64;eighths<=256;eighths++) {
        const double trial=eighths/8.;
        pango_font_description_set_absolute_size(font,trial*PANGO_SCALE);
        pango_layout_set_font_description(layout,font);
        const double err=std::abs(width("MMMMMMMM")-reference);
        if(err<best) {best=err;size=trial;}
    }
    pango_font_description_set_absolute_size(font,size*PANGO_SCALE);
    pango_layout_set_font_description(layout,font);
    const int result=static_cast<int>(width(prefixText.c_str())+.5);
    pango_font_description_free(font);g_object_unref(layout);g_object_unref(context);
    return result;
}
void resetText() {
    loading=true; call(SCI_AUTOCCANCEL);call(SCI_SETTEXT,0,str(fixture.c_str()));
    call(SCI_COLOURISE,0,-1);call(SCI_EMPTYUNDOBUFFER);call(SCI_SETSAVEPOINT);
    loading=false;
}
struct Stats { double median, p95; };
template<class F> Stats measure(int count,F test) {
    using Clock=std::chrono::steady_clock;
    std::vector<double> samples;
    for(int i=0;i<=count;i++) {
        const auto start=Clock::now();
        test();
        const auto end=Clock::now();
        if(i) samples.push_back(std::chrono::duration<double,std::micro>(end-start).count());
    }
    std::sort(samples.begin(),samples.end());
    return {samples[count/2],samples[static_cast<size_t>(std::ceil(.95*count))-1]};
}
std::string benchmark() {
    call(SCI_AUTOCCANCEL);
    const auto a=std::max<intptr_t>(0,dragStart);
    call(SCI_GOTOPOS,a);
    const auto y=call(SCI_POINTYFROMPOSITION,0,a);
    const auto x=call(SCI_POINTXFROMPOSITION,0,a);
    bool realHit=call(SCI_POSITIONFROMPOINTCLOSE,x,y+2)==a;
    const Stats hit=measure(31,[&] {
        for(int i=0;i<256;i++) call(SCI_POSITIONFROMPOINTCLOSE,x+(i%37),y+2);
    });
    // Identical prebuilt 1 MiB buffer, only two identifiers match the prefix.
    const std::string line="  \"pad\": \"0123456789abcdefghijklmnopqrstuvwxyz\",\n";
    std::string data="{\n";
    while(data.size()<1024*1024) data+=line;
    data+="  \""+alpha+"\":1,\n  \""+beta+"\":2\n}\n"+prefix;
    call(SCI_SETWRAPMODE,SC_WRAP_NONE);call(SCI_SETTEXT,0,str(data.c_str()));
    call(SCI_COLOURISE,0,-1);call(SCI_GOTOPOS,data.size());
    bool filled=true;
    const Stats completion=measure(17,[&] {
        call(SCI_AUTOCCANCEL);candidates.clear();complete();
        filled=filled && call(SCI_AUTOCACTIVE) && candidates.find(alpha)!=candidates.npos && candidates.find(beta)!=candidates.npos;
    });
    call(SCI_AUTOCCANCEL);
    std::ostringstream o; o<<"ok="<<(filled&&realHit)<<"\nvalidHit="<<realHit<<"\nbytes="<<data.size()
      <<"\nhit256MedianUs="<<hit.median<<"\nhit256P95Us="<<hit.p95
      <<"\ncompletionMedianUs="<<completion.median<<"\ncompletionP95Us="<<completion.p95;
    return o.str();
}
std::string state(const std::string &action,int ox,int oy) {
    std::ostringstream o;
    const auto height=call(SCI_TEXTHEIGHT,0);
    if (action=="bench") return benchmark();
    if (action=="ready") {
        const auto pos=fixture.find("😀");
        Sci_Position width=0;
        o<<"forkChar="<<call(SCI_GETCHARACTERANDWIDTH,pos,reinterpret_cast<intptr_t>(&width))<<"\nforkWidth="<<width;
        o<<"\nfoldX="<<ox+11<<"\nfoldY="<<oy+call(SCI_POINTYFROMPOSITION,0,call(SCI_POSITIONFROMLINE,1))+height/2;
        o<<"\nheader="<<!!(call(SCI_GETFOLDLEVEL,1)&SC_FOLDLEVELHEADERFLAG)<<"\nlexer="<<call(SCI_GETLEXER);
    } else if(action=="dragplan") {
        const auto lineStart=call(SCI_POSITIONFROMLINE,5);
        int y=0,x1=-1,x2=-1;
        for(auto p=fixture.find(marker);p!=std::string::npos;p=fixture.find(marker,p+marker.size())) {
            const auto py=call(SCI_POINTYFROMPOSITION,0,p);
            if(py<=call(SCI_POINTYFROMPOSITION,0,lineStart)||py!=call(SCI_POINTYFROMPOSITION,0,p+marker.size()-1)) continue;
            if(py>=height*(call(SCI_LINESONSCREEN)-1)) continue;
            const auto x=call(SCI_POINTXFROMPOSITION,0,p);
            x1=ox+x+advance("Aé");x2=ox+x+advance("AéB中😀");y=oy+py+height/2;
            dragStart=p+std::string("Aé").size();dragEnd=p+marker.size()-1;
            o<<"modelX1="<<ox+call(SCI_POINTXFROMPOSITION,0,dragStart)<<"\nmodelX2="<<ox+call(SCI_POINTXFROMPOSITION,0,dragEnd)<<"\n";
            break;
        }
        o<<"folded="<<!call(SCI_GETLINEVISIBLE,2)<<"\nwraps="<<call(SCI_WRAPCOUNT,5)<<"\nx1="<<x1<<"\nx2="<<x2<<"\ny="<<y;
    } else if(action=="selection") {
        const auto a=call(SCI_GETSELECTIONSTART), b=call(SCI_GETSELECTIONEND);
        const auto selected=text().substr(a,b-a);
        o<<"start="<<a<<"\nend="<<b<<"\nexpectedStart="<<dragStart<<"\nexpectedEnd="<<dragEnd<<"\nselectedHex="<<encode(selected)<<"\nok="<<(a==dragStart&&b==dragEnd&&selected=="B中😀");
    } else if (action=="mutate") {
        const auto pos=fixture.find(beta);
        if(pos==fixture.npos) return "ok=0";
        call(SCI_AUTOCCANCEL);
        call(SCI_SETTARGETRANGE,fixture.size(),call(SCI_GETLENGTH));call(SCI_REPLACETARGET,0,str(""));
        const std::string zeta="np4ProofCandidateZeta";
        call(SCI_SETTARGETRANGE,pos,pos+beta.size());call(SCI_REPLACETARGET,zeta.size(),str(zeta.c_str()));
        fixture.replace(pos,beta.size(),zeta);call(SCI_GOTOPOS,fixture.size());
        candidates.clear();o<<"ok="<<(text()==fixture);
    } else if(action=="prefix") {
        // Retain the folded document. Remove only the editable test suffix.
        call(SCI_AUTOCCANCEL); call(SCI_SETTARGETRANGE,fixture.size(),call(SCI_GETLENGTH));call(SCI_REPLACETARGET,0,str(""));
        call(SCI_GOTOPOS,fixture.size());call(SCI_EMPTYUNDOBUFFER);
        candidates.clear();accepts=tentative=committed=0;o<<"ok=1";
    } else if(action=="completion") {
        char first[512]{}; call(SCI_AUTOCGETCURRENTTEXT,0,reinterpret_cast<intptr_t>(first));
        o<<"active="<<call(SCI_AUTOCACTIVE)<<"\nfirst="<<first<<"\nlistedAlpha="<<(candidates.find(alpha)!=candidates.npos)<<"\nlistedBeta="<<(candidates.find(beta)!=candidates.npos);
        o<<"\nlistedZeta="<<(candidates.find("np4ProofCandidateZeta")!=candidates.npos);
        o<<"\ncandidates=";for(char c:candidates) o<<(c=='\n'?'|':c);
        o<<"\ntargetHex="<<encode(text().substr(fixture.size()));
    } else if(action=="accepted"||action=="undo"||action=="ime") {
        const auto t=text(); const auto suffix=t.size()>=fixture.size()?t.substr(fixture.size()):"<short>";
        const auto expected=action=="accepted"?beta:action=="undo"?prefix:prefix+"你好";
        o<<"targetHex="<<encode(suffix)<<"\nsourceTentative="<<tentative<<"\nsourceResult="<<committed<<"\naccepts="<<accepts<<"\nwrongPopupAccepts="<<(action=="ime"?accepts:0)<<"\nok="<<(t==fixture+expected && (action!="ime"||!accepts));
    } else o<<"error=unknown command";
    return o.str();
}
}
intptr_t dispatch(unsigned msg,uintptr_t w,intptr_t l) {
    if(msg==SCI_AUTOCSHOW && l) candidates=reinterpret_cast<const char*>(l);
    return send(msg,w,l);
}
void attach(Send f,const char *path) {
    send=std::move(f);
    if(const char *d=std::getenv("NP4_QA_DIR")) exchange=d;
    fixture=path && *path?readFile(path):sample();
    call(SCI_SETCODEPAGE,SC_CP_UTF8);
    call(SCI_SETLEXER,SCLEX_JSON);
    call(SCI_SETPROPERTY,reinterpret_cast<uintptr_t>("fold"),str("1"));
    call(SCI_STYLESETFONT,STYLE_DEFAULT,str("DejaVu Sans Mono"));call(SCI_STYLESETSIZE,STYLE_DEFAULT,14);
    call(SCI_STYLESETFORE,STYLE_DEFAULT,0x202020);call(SCI_STYLESETBACK,STYLE_DEFAULT,0xffffff);call(SCI_STYLECLEARALL);
    call(SCI_STYLESETFORE,SCE_JSON_PROPERTYNAME,0x884400);call(SCI_STYLESETFORE,SCE_JSON_STRING_DQ,0x228822);
    call(SCI_SETMARGINS,1);call(SCI_SETMARGINTYPEN,0,SC_MARGIN_SYMBOL);call(SCI_SETMARGINWIDTHN,0,22);
    call(SCI_SETMARGINMASKN,0,SC_MASK_FOLDERS);call(SCI_SETMARGINSENSITIVEN,0,1);
    call(SCI_MARKERDEFINE,SC_MARKNUM_FOLDER,SC_MARK_PLUS);call(SCI_MARKERDEFINE,SC_MARKNUM_FOLDEROPEN,SC_MARK_MINUS);
    call(SCI_SETAUTOMATICFOLD,SC_AUTOMATICFOLD_SHOW|SC_AUTOMATICFOLD_CLICK|SC_AUTOMATICFOLD_CHANGE);
    call(SCI_SETWRAPMODE,SC_WRAP_WORD);call(SCI_SETIMEINTERACTION,SC_IME_INLINE);
    call(SCI_AUTOCSETSEPARATOR,'\n');configureCompletion();resetText();
    call(SCI_GOTOPOS,0);call(SCI_SETFIRSTVISIBLELINE,0);
}
void notify(unsigned code,int ch,int source,int handled) {
    if(!send||loading) return;
    if(code==SCN_CHARADDED) {
        tentative+=source==SC_CHARACTERSOURCE_TENTATIVE_INPUT;
        committed+=source==SC_CHARACTERSOURCE_IME_RESULT;
        typed(static_cast<unsigned>(ch),source,handled);
    } else if(code==SCN_AUTOCSELECTION) ++accepts;
}
void tick(int x,int y) {
    if(!send||exchange.empty()) return;
    std::ifstream input(exchange/"request"); int sequence;std::string action;
    if(!(input>>sequence>>action)||lastSequence==sequence) return;
    lastSequence=sequence;
    const auto reply=state(action,x,y);
    { std::ofstream file(exchange/"reply.tmp"); file<<sequence<<"\n"<<reply<<"\n"; }
    fs::rename(exchange/"reply.tmp",exchange/"reply");
}
}
