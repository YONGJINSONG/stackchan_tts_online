#include "SpatialRenderer.h"
#include <algorithm>

namespace {
void splitRows(const String& s, std::vector<String>& out) {
  out.clear(); int p=0;
  while (p <= s.length()) { int n=s.indexOf('/',p); if(n<0)n=s.length(); out.push_back(s.substring(p,n)); p=n+1; if(n>=s.length()) break; }
}
void splitTokens(const String& s, std::vector<String>& out) {
  out.clear(); int p=0;
  while (p <= s.length()) { int n=s.indexOf(',',p); if(n<0)n=s.length(); String t=s.substring(p,n); t.trim(); out.push_back(t); p=n+1; if(n>=s.length()) break; }
}
int utf8Bytes(const String& s,int p){uint8_t c=(uint8_t)s[p]; if(c<0x80)return 1; if((c&0xE0)==0xC0)return 2; if((c&0xF0)==0xE0)return 3; return 4;}
}

void SpatialRenderer::drawStatus(const String& subject,uint8_t level,uint32_t stars){
  M5.Display.fillRect(0,0,320,24,TFT_BLACK); M5.Display.setTextColor(TFT_WHITE,TFT_BLACK);
  M5.Display.drawString(subject+"  LV"+String(level)+"  *"+String(stars),8,5); M5.Display.drawFastHLine(0,24,320,TFT_DARKGREY);
}

void SpatialRenderer::drawFooter(){
  const char* labels[3]={"이전","정답","다음"}; const int top=204, third=320/3;
  M5.Display.fillRect(0,top,320,36,TFT_BLACK);
  for(int i=0;i<3;i++){int l=i*third+3,r=(i==2)?317:(i+1)*third-3; M5.Display.drawRoundRect(l,top+3,r-l,29,5,TFT_DARKGREY); int tw=M5.Display.textWidth(labels[i]); M5.Display.drawString(labels[i],l+((r-l)-tw)/2,top+10);}
}

void SpatialRenderer::drawWrapped(const String& text,int x,int y,int maxWidth,int lineHeight,int maxLines){
  int p=0; for(int line=0;line<maxLines&&p<text.length();line++){String part; while(p<text.length()){int b=utf8Bytes(text,p); if(p+b>text.length())b=1; String c=part+text.substring(p,p+b); if(part.length()&&M5.Display.textWidth(c)>maxWidth)break; part=c;p+=b;} part.trim(); int tw=M5.Display.textWidth(part); M5.Display.drawString(part,x+std::max(0,(maxWidth-tw)/2),y+line*lineHeight); while(p<text.length()&&text[p]==' ')p++;}
}

void SpatialRenderer::drawGrid(const String& encoded,int cx,int cy,int maxW,int maxH,uint16_t color){
  std::vector<String> rows; splitRows(encoded,rows); if(rows.empty())return; int cols=0; for(auto&r:rows)cols=std::max(cols,(int)r.length()); if(!cols)return;
  int cell=std::max(5,std::min(20,std::min(maxW/cols,maxH/(int)rows.size()))); int x0=cx-cols*cell/2,y0=cy-(int)rows.size()*cell/2;
  for(int r=0;r<(int)rows.size();r++)for(int c=0;c<(int)rows[r].length();c++)if(rows[r][c]=='1'||rows[r][c]=='#'){M5.Display.fillRoundRect(x0+c*cell+1,y0+r*cell+1,cell-2,cell-2,2,color);M5.Display.drawRoundRect(x0+c*cell+1,y0+r*cell+1,cell-2,cell-2,2,TFT_WHITE);}
}

void SpatialRenderer::drawToken(const String& token,int cx,int cy,int size,uint16_t color){String t=token;t.toUpperCase(); if(t=="C"){M5.Display.fillCircle(cx,cy,size/2,color);M5.Display.drawCircle(cx,cy,size/2,TFT_WHITE);} else if(t=="T"){M5.Display.fillTriangle(cx,cy-size/2,cx-size/2,cy+size/2,cx+size/2,cy+size/2,color);M5.Display.drawTriangle(cx,cy-size/2,cx-size/2,cy+size/2,cx+size/2,cy+size/2,TFT_WHITE);} else {M5.Display.fillRoundRect(cx-size/2,cy-size/2,size,size,3,color);M5.Display.drawRoundRect(cx-size/2,cy-size/2,size,size,3,TFT_WHITE);}}

void SpatialRenderer::drawPattern(const String& encoded,int x,int y,int w,int h){std::vector<String> t;splitTokens(encoded,t);if(t.empty())return;int step=w/(int)t.size();for(int i=0;i<(int)t.size();i++){int cx=x+i*step+step/2,cy=y+h/2;if(t[i]=="?"){M5.Display.setTextColor(TFT_YELLOW,TFT_BLACK);M5.Display.drawString("?",cx-5,cy-9);M5.Display.setTextColor(TFT_WHITE,TFT_BLACK);}else drawToken(t[i],cx,cy,std::min(20,step-4),TFT_CYAN);}}

void SpatialRenderer::drawHeightMap(const String& encoded,int x,int y,int w,int h){std::vector<String> rows;splitRows(encoded,rows);if(rows.empty())return;int cols=0;for(auto&r:rows)cols=std::max(cols,(int)r.length());const int cell=15,dy=11,rowDx=6;int bx=x+(w-(cols*cell+(int)rows.size()*rowDx))/2,by=y+h-8;for(int r=0;r<(int)rows.size();r++)for(int c=0;c<(int)rows[r].length();c++){int zmax=(rows[r][c]>='0'&&rows[r][c]<='9')?rows[r][c]-'0':0;for(int z=0;z<zmax;z++){int px=bx+c*cell+r*rowDx,py=by-r*6-z*dy-cell;M5.Display.fillRect(px+1,py+1,cell-2,cell-2,TFT_CYAN);M5.Display.drawRect(px,py,cell,cell,TFT_WHITE);}}}

void SpatialRenderer::drawQuestion(const Question& q,const std::vector<String>& displayChoices,int selected,const String& subject,uint8_t level,uint32_t stars){
  M5.Display.fillScreen(TFT_BLACK);M5.Display.setFont(&fonts::efontKR_16);M5.Display.setTextSize(1);M5.Display.setTextColor(TFT_WHITE,TFT_BLACK);drawStatus(subject,level,stars);drawWrapped(q.question,8,30,304,17,2);
  const int mainY=62;
  if(q.visualType=="pattern")drawPattern(q.visualData,20,mainY,280,64); else if(q.visualType=="heightmap"||q.visualType=="top_view")drawHeightMap(q.visualData,8,mainY,304,64); else {int cx=(q.domain=="mirror")?125:160;drawGrid(q.visualData,cx,94,90,58,TFT_CYAN);if(q.domain=="mirror")M5.Display.drawFastVLine(184,65,56,TFT_YELLOW);if(q.domain=="rotation"){M5.Display.drawArc(216,91,20,16,210,60,TFT_YELLOW);M5.Display.fillTriangle(232,84,239,83,235,90,TFT_YELLOW);}}
  const int gap=6,boxY=132,boxH=66,boxW=(320-gap*4)/3;
  for(int i=0;i<3;i++){int bx=gap+i*(boxW+gap);uint16_t col=(i==selected)?TFT_YELLOW:TFT_DARKGREY;M5.Display.drawRoundRect(bx,boxY,boxW,boxH,5,col);M5.Display.setTextColor((i==selected)?TFT_YELLOW:TFT_WHITE,TFT_BLACK);M5.Display.drawString(String(i+1),bx+5,boxY+4);M5.Display.setTextColor(TFT_WHITE,TFT_BLACK);
    if(i<(int)q.visualChoices.size()){const String& v=q.visualChoices[i];if(q.visualType=="pattern")drawToken(v,bx+boxW/2,boxY+40,24,(i==selected)?TFT_YELLOW:TFT_CYAN);else drawGrid(v,bx+boxW/2,boxY+40,boxW-18,44,(i==selected)?TFT_YELLOW:TFT_CYAN);} else if(i<(int)displayChoices.size()){int tw=M5.Display.textWidth(displayChoices[i]);M5.Display.setTextColor((i==selected)?TFT_YELLOW:TFT_WHITE,TFT_BLACK);M5.Display.drawString(displayChoices[i],bx+(boxW-tw)/2,boxY+31);M5.Display.setTextColor(TFT_WHITE,TFT_BLACK);}}
  drawFooter();
}
