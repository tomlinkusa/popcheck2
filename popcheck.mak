# popcheck.mak
# Created by IBM WorkFrame/2 MakeMake at 21:42:38 on 03/06/97
#
# The actions included in this make file are:
#  Compile
#  Resource Compile
#  Link

.SUFFIXES: .rc .res

.all: \
    popcheck.exe

.rc.res:
    @echo " Resource Compile "
    rc.exe -r %s

popcheck.obj: \
    popcheck.C \
    popcheck.H
    @echo " Compile "
    cl.exe /c /W3 /MT /Zi $(COPTS) popcheck.c

popcheck.exe: \
    popcheck.obj \
    popcheck.res
    @echo " Link "
    link.exe @<<
     $(LOPTS) /SUBSYSTEM:WINDOWS
     popcheck.obj
     wsock32.lib kernel32.lib gdi32.lib user32.lib comctl32.lib advapi32.lib shell32.lib Winmm.lib Comdlg32.lib
     popcheck.res
<<

popcheck.res: \
   popcheck.ico \
   popcheck2.ico \
   popcheck.rc \
   libm.bmp \
    popcheck.H

