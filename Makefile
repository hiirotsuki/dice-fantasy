CFLAGS = /nologo /O2 /W4 /WX /MT /GS- /Brepro
LDFLAGS = /nologo /DLL /Brepro /INCREMENTAL:NO
LIBS = user32.lib gdi32.lib kernel32.lib
OBJS = main.obj dinput.obj patch.obj translate.obj dialog.obj truecolor.obj text.obj

dinput.dll: $(OBJS)
	link $(LDFLAGS) /OUT:$@ $(OBJS) $(LIBS)

$(OBJS): wrapper.h

clean:
	-del /q $(OBJS) dinput.dll dinput.lib dinput.exp 2>nul
