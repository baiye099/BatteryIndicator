#include "../src/battery.c"
int main(void) {
    const int width=940,height=520;DWORD *px;
    BITMAPINFO bi={0};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=width;bi.bmiHeader.biHeight=-height;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;
    HDC dc=CreateCompatibleDC(NULL);HBITMAP bm=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,(void**)&px,NULL,0);HGDIOBJ old=SelectObject(dc,bm);
    HBRUSH bg=CreateSolidBrush(RGB(242,245,249));RECT all={0,0,width,height};FillRect(dc,&all,bg);DeleteObject(bg);
    HFONT label=CreateFontW(-18,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Segoe UI");HGDIOBJ oldFont=SelectObject(dc,label);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(30,40,55));
    TextOutW(dc,20,16,L"Tray font preview | actual 16px + enlarged 4x",45);
    TextOutW(dc,280,50,L"Dark tray",9);TextOutW(dc,495,50,L"Light tray",10);TextOutW(dc,720,50,L"100 (4x view)",13);
    for(DWORD f=0;f<_countof(fontChoices);f++) {
        int y=90+(int)f*58;TextOutW(dc,20,y+10,fontChoices[f].face,(int)wcslen(fontChoices[f].face));
        for(int theme=0;theme<2;theme++) {
            int x=270+theme*215;RECT r={x,y,x+180,y+42};HBRUSH br=CreateSolidBrush(theme?RGB(255,255,255):RGB(28,31,38));FillRect(dc,&r,br);DeleteObject(br);
            int values[]={39,100,8,20,-1};
            for(int k=0;k<5;k++) {HICON icon=renderIcon(values[k],0,16,f);DrawIconEx(dc,x+10+k*32,y+13,icon,16,16,0,NULL,DI_NORMAL);DestroyIcon(icon);}
        }
        HDC sample=CreateCompatibleDC(dc);HBITMAP sb=CreateCompatibleBitmap(dc,16,16);HGDIOBJ so=SelectObject(sample,sb);
        HBRUSH br=CreateSolidBrush(RGB(28,31,38));RECT sr={0,0,16,16};FillRect(sample,&sr,br);DeleteObject(br);
        HICON icon=renderIcon(100,0,16,f);DrawIconEx(sample,0,0,icon,16,16,0,NULL,DI_NORMAL);SetStretchBltMode(dc,COLORONCOLOR);StretchBlt(dc,730,y-4,64,64,sample,0,0,16,16,SRCCOPY);DestroyIcon(icon);SelectObject(sample,so);DeleteObject(sb);DeleteDC(sample);
    }
    GdiFlush();FILE *out=NULL;fopen_s(&out,"bin/font-preview.bmp","wb");if(!out) return 1;
    BITMAPFILEHEADER header={0};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+width*height*4;
    fwrite(&header,sizeof(header),1,out);fwrite(&bi.bmiHeader,sizeof(bi.bmiHeader),1,out);fwrite(px,width*height*4,1,out);fclose(out);
    SelectObject(dc,oldFont);SelectObject(dc,old);DeleteObject(label);DeleteObject(bm);DeleteDC(dc);return 0;
}
