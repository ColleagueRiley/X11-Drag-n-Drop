// This compiles with
// gcc example.c -lX11

#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdint.h>
#include <limits.h>

int main(void) {
    Display* display = XOpenDisplay(NULL);
 
    Window window = XCreateSimpleWindow(display, 
										RootWindow(display, DefaultScreen(display)), 
										10, 10, 200, 200, 1,
										BlackPixel(display, DefaultScreen(display)), WhitePixel(display, DefaultScreen(display)));
 
    XSelectInput(display, window, ExposureMask | KeyPressMask);

	const Atom wm_delete_window = XInternAtom((Display*) display, "WM_DELETE_WINDOW", False);
	
	/* Xdnd code */
	
	/* fetching data */
	const Atom XdndTypeList = XInternAtom(display, "XdndTypeList", False);
	const Atom XdndSelection = XInternAtom(display, "XdndSelection", False);

	/* client messages */
	const Atom XdndEnter = XInternAtom(display, "XdndEnter", False);
	const Atom XdndPosition = XInternAtom(display, "XdndPosition", False);
	const Atom XdndStatus = XInternAtom(display, "XdndStatus", False);
	const Atom XdndLeave = XInternAtom(display, "XdndLeave", False);	
	const Atom XdndDrop = XInternAtom(display, "XdndDrop", False);	
	const Atom XdndFinished = XInternAtom(display, "XdndFinished", False);

	/* actions */
	const Atom XdndActionCopy = XInternAtom(display, "XdndActionCopy", False);
	const Atom XdndActionMove = XInternAtom(display, "XdndActionMove", False);
	const Atom XdndActionLink = XInternAtom(display, "XdndActionLink", False);
	const Atom XdndActionAsk = XInternAtom(display, "XdndActionAsk", False);
	const Atom XdndActionPrivate = XInternAtom(display, "XdndActionPrivate", False);

	const Atom XdndAware = XInternAtom(display, "XdndAware", False);
	const char myVersion = 5;
	XChangeProperty(display, window, XdndAware, 4, 32, PropModeReplace, &myVersion, 1);

    XMapWindow(display, window);
	
	XEvent E;
	Bool running = True;

	int64_t source, version;
	int32_t format;

    while (running) {
        XNextEvent(display, &E);

        switch (E.type) {
			case KeyPress: running = False; break;
			case ClientMessage:
				if (E.xclient.data.l[0] == (int64_t) wm_delete_window) {
					running = False;
					break;
				}
				
				if (E.xclient.message_type == XdndEnter) {
					unsigned long count;
					Atom* formats;
					Atom real_formats[6];

					Bool list = E.xclient.data.l[1] & 1;

					source = E.xclient.data.l[0];
					version = E.xclient.data.l[1] >> 24;
					format = None;

					if (version > 5)
						break;

					if (list) {
						Atom actualType;
						int32_t actualFormat;
						unsigned long bytesAfter;

						XGetWindowProperty((Display*) display,
							source,
							XdndTypeList,
							0,
							LONG_MAX,
							False,
							4,
							&actualType,
							&actualFormat,
							&count,
							&bytesAfter,
							(unsigned char**) &formats);
					} else {
						count = 0;

						if (E.xclient.data.l[2] != None)
							real_formats[count++] = E.xclient.data.l[2];
						if (E.xclient.data.l[3] != None)
							real_formats[count++] = E.xclient.data.l[3];
						if (E.xclient.data.l[4] != None)
							real_formats[count++] = E.xclient.data.l[4];
						
						formats = real_formats;
					}

					uint32_t i, j;
					for (i = 0; i < (uint32_t)count; i++) {
						char* name = XGetAtomName((Display*) display, formats[i]);

						char* links[2] = {"text/uri-list", "text/plain" };
						for (j = 0; j < 2; j++) {
							if (strcmp(name, links[j]) == 0) {
								format = formats[i];
								i = count + 1;
								break;
							}
						}
					}
					
					if (list) {
						XFree(formats);
					}

					break;
				}
				if (E.xclient.message_type == XdndPosition) {
					const int32_t xabs = (E.xclient.data.l[2] >> 16) & 0xffff;
					const int32_t yabs = (E.xclient.data.l[2]) & 0xffff;
					Window dummy;
					int32_t xpos, ypos;

					if (version > 5)
						break;

					XTranslateCoordinates((Display*) display,
						XDefaultRootWindow((Display*) display),
						(Window) window,
						xabs, yabs,
						&xpos, &ypos,
						&dummy);
					
					printf("File drop starting at %i %i\n", xpos, ypos);
					
					XEvent reply = { ClientMessage };
					reply.xclient.window = source;
					reply.xclient.message_type = XdndStatus;
					reply.xclient.format = 32;
					reply.xclient.data.l[0] = (long) window;
					reply.xclient.data.l[2] = 0;
					reply.xclient.data.l[3] = 0;

					if (format) {
						reply.xclient.data.l[1] = 1;
						if (version >= 2)
							reply.xclient.data.l[4] = XdndActionCopy;
					}

					XSendEvent((Display*) display, source, False, NoEventMask, &reply);
					XFlush((Display*) display);
					break;
				}

				if (E.xclient.message_type = XdndDrop && version <= 5) {
					if (format) {
						Time time = CurrentTime;

						if (version >= 1)
							time = E.xclient.data.l[2];

						XConvertSelection((Display*) display,
							XdndSelection,
							format,
							XdndSelection,
							(Window) window,
							time);
					} else if (version >= 2) {
						XEvent reply = { ClientMessage };
						reply.xclient.window = source;
						reply.xclient.message_type = XdndFinished;
						reply.xclient.format = 32;
						reply.xclient.data.l[0] = (long) window;
						reply.xclient.data.l[1] = 0;
						reply.xclient.data.l[2] = None;

						XSendEvent((Display*) display, source,
							False, NoEventMask, &reply);
						XFlush((Display*) display);
					}
				}
				break;
		case SelectionNotify: {
			/* this is only for checking for drops */
			if (E.xselection.property != XdndSelection)
				break;

			char* data;
			unsigned long result;

			Atom actualType;
			int32_t actualFormat;
			unsigned long bytesAfter;

			XGetWindowProperty((Display*) display, 
											E.xselection.requestor, E.xselection.property, 
											0, LONG_MAX, False, E.xselection.target, 
											&actualType, &actualFormat, &result, &bytesAfter, 
											(unsigned char**) &data);

			if (result == 0)
				break;

			printf("File(s) dropped: %s\n", data);

			if (data)
				XFree(data);

			if (version >= 2) {
				XEvent reply = { ClientMessage };
				reply.xclient.window = source;
				reply.xclient.message_type = XdndFinished;
				reply.xclient.format = 32;
				reply.xclient.data.l[0] = (long) window;
				reply.xclient.data.l[1] = result;
				reply.xclient.data.l[2] = XdndActionCopy;

				XSendEvent((Display*) display, source, False, NoEventMask, &reply);
				XFlush((Display*) display);
			}

			break;
		}

			default: break;
		}
    }
 
    XCloseDisplay(display);
}
