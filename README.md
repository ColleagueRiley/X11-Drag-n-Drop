# RGFW Under the Hood: X11 Drag 'n Drop

## Introduction
To handle Drag 'n Drop events with X11, you must use the XDnD protocol. Although the XDnD protocol is significantly more complicated than other Drag 'n Drop APIs, it's still relatively simple in theory. Although the theory is simple, implementing it is very tedious. You must properly communicate with the X11 server and the source window to get your desired results.

This tutorial explains how to handle the XDnD protocol and manage X11 Drag 'n Drop events. The code is based on RGFW's [source code](https://github.com/ColleagueRiley/RGFW).

## Overview
A detailed overview of the steps required:

First, [X11 Atoms](https://tronche.com/gui/x/xlib/window-information/properties-and-atoms.html) will be initiated. X11 Atoms are used to ask for or send specific data or properties through X11. 
Then, the window's properties will be changed, allowing it to be aware of [XDND](https://freedesktop.org/wiki/Specifications/XDND/) (X Drag 'n Drop) events. 
When a drop happens, the window will receive a [`ClientMessage`](https://www.x.org/releases/X11R7.5/doc/man/man3/XClientMessageEvent.3.html) Event which includes an `XdndEnter` telling the target window that the a has started.
While the drop is in progress, the source window sends updates about the drop to the target window via ClientMessage events. Each time the target window gets an update, it must confirm it received the update; otherwise, the interaction will end. 
Once the drop happens, the target window will receive a [`SelectionNotify`](https://www.x.org/releases/X11R7.5/doc/man/man3/XSelectionEvent.3.html) event. 
The target window will handle this event, convert the data to a readable string, and then send a ClientMessage with the `XdndFinished` atom to tell the source window that the interaction is done. 

A quick overview of the steps required:

1) Define X11 Atoms
2) Enable XDnD events for the window
3) Handle XDnD events via `ClientMessage`
4) Get XDnD drop data via `ClientMessage` and end the interaction

# Step 1 (Define X11 Atoms)
To handle XDnD events, XDnD atoms must be initialized via [`XInternAtom`](https://www.x.org/releases/X11R7.5/doc/man/man3/XInternAtom.3.html). Atoms are used when sending or requesting specific data or actions. 

`XdndTypeList` is used when the target window wants to know the data types the source window supports. 
`XdndSelection` is used to examine the data during a drag and to retrieve the data after a drop.

```c
const Atom XdndTypeList = XInternAtom(display, "XdndTypeList", False);
const Atom XdndSelection = XInternAtom(display, "XdndSelection", False);
```

These generic `Xdnd` atoms are messages sent by the source window.

`XdndEnter`, is used when the drop has entered the target window
`XdndPosition` is used to update the target window on the position of the drop
`XdndStatus` is used  
`XdndLeave` is used when the drop has left the target window 
`XdndDrop` is used when the drop has been dropped into the target window
`XdndFinished` is used when the drop has been finished

```c
const Atom XdndEnter = XInternAtom(display, "XdndEnter", False);
const Atom XdndPosition = XInternAtom(display, "XdndPosition", False);
const Atom XdndStatus = XInternAtom(display, "XdndStatus", False);
const Atom XdndLeave = XInternAtom(display, "XdndLeave", False);	
const Atom XdndDrop = XInternAtom(display, "XdndDrop", False);	
const Atom XdndFinished = XInternAtom(display, "XdndFinished", False);
```

Xdnd Actions are actions the target window wants to make with the drag data.

`XdndActionCopy` is used when the target window wants to copy the drag data 

```c
const Atom XdndActionCopy = XInternAtom(display, "XdndActionCopy", False);
```

# Step 2 (Enable XDnD events for the window)

To receive XDnD events, the window must enable the `XDndAware` atom. This atom tells the window manager and the source window that the window wants to receive XDnD events.

This can be done by creating an `XdndAware` atom and using [`XChangeProperty`](https://tronche.com/gui/x/xlib/window-information/XChangeProperty.html) to change the window's `XdndAware` property.

You also must set the XDnD version using a pointer, version 5 should be used as it is the newest version of the XDnD protocol.

```c
const Atom XdndAware = XInternAtom(display, "XdndAware", False);
const char myversion = 5;

XChangeProperty(display, window, XdndAware, 4, 32, PropModeReplace, &myversion, 1);
```

# Step 3 (Handle XDnD events via ClientMessage)
Before any events are handled, some variables must be defined. 
These variables are given to us by the source window and are used across multiple instances.  

These variables are the source (of the drop), the XDnD Protocall version used, and the format of the drop data.

```c
int64_t source, version;
int32_t format;
```

The [`ClientMessage`](E.xclient.message_type)  event can be handled.

```c
case ClientMessage:
```

The ClientMessage event structure can be accessed via `XEvent.xclient`.

`message_type` is an attribute in the structure, it holds what the message type is. We will use it to check if the message type is an XDnD message.

There are 3 XDnD events we will handle, `XdndEnter`, `XdndPosition`, and `XdndDrop`.

### Step 3.1 (XdndEnter)

XdndEnter is sent when the drop enters the target window.

```c
if (E.xclient.message_type == XdndEnter) {
```

First, RGFW inits the required variables.

* count for the count of the format list, 
* formats, the list of supported formats and 
* real_formats which is used here to avoid running `malloc` for each drop

```c
    unsigned long count;
    Atom* formats;
    Atom real_formats[6];
```

We can also create a bool to check if the format is a list or if there is only one format.

This can be done by using the xclient's `data` attribute. Data is a list of data about the event. 

the first item is the source window.

The second item of the data includes two values, if the format is a list or not and the version of XDnD used.  
To get the bool value, you can check the first bit, the version is stored 24 bits after (the final 40 bits). 

The format should be set to None for now, also make sure the version is less than or equal to 5. Otherwise, there's probably an issue because 5 is the newest version.

```c
    Bool list = E.xclient.data.l[1] & 1;

    source = E.xclient.data.l[0];
    version = E.xclient.data.l[1] >> 24;
    format = None;

    if (version > 5)
        break;
```

If the format is a list, we'll have to get the format list from the source window's `XDndTypeList` value using [XGetWindowProperty](https://www.x.org/releases/X11R7.5/doc/man/man3/XChangeProperty.3.html)

```c
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
    } 
```

Otherwise, the format can be found using the leftover xclient values (2 - 4)

```c
    else {
        count = 0;

        if (E.xclient.data.l[2] != None)
            real_formats[count++] = E.xclient.data.l[2];
        if (E.xclient.data.l[3] != None)
            real_formats[count++] = E.xclient.data.l[3];
        if (E.xclient.data.l[4] != None)
            real_formats[count++] = E.xclient.data.l[4];
        
        formats = real_formats;
    }
```

Now that we have the format array, we can check if the format matches any of the formats we're looking for.

The list should also be freed using [`XFree`](https://software.cfht.hawaii.edu/man/x11/XFree(3x)) if it was received using `XGetWindowProperty`.

```c
    uint32_t i, j;
    for (i = 0; i < (uint32_t)count; i++) {
        char* name = XGetAtomName((Display*) display, formats[i]);

        char* links[2] = { "text/uri-list", "text/plain" };
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
```

### Step 3.2 (XdndPosition)
XdndPosition is used when the drop position is updated.

Before we handle the event, make sure the version is correct.

```c
if (E.xclient.message_type == XdndPosition && version <= 5)) {
```

The absolute X and Y can be found using the second item of the data list.

The X = the last 32 bits.
The Y = the first 32 bits.

```c
    const int32_t xabs = (E.xclient.data.l[2] >> 16) & 0xffff;
    const int32_t yabs = (E.xclient.data.l[2]) & 0xffff;
```

Now the absolute X and Y can be translated to the actual X and Y coordinates of the drop position using [XTranslateCoordinates](https://tronche.com/gui/x/xlib/window-information/XTranslateCoordinates.html).

```c
    Window dummy;
    int32_t xpos, ypos;

    XTranslateCoordinates((Display*) display,
        XDefaultRootWindow((Display*) display),
        (Window) window,
        xabs, yabs,
        &xpos, &ypos,
        &dummy);
   
    printf("File drop starting at %i %i\n", xpos, ypos);
```

Now a response must be sent back to the source window. The response uses `XdndStatus` to tell the window it has recvied the message. 

We should also tell the source the action accepted will the data. (`XdndActionCopy`)

The message can be sound out via [`XSendEvent`](https://tronche.com/gui/x/xlib/event-handling/XSendEvent.html) make sure you also send out [`XFlush`](https://www.x.org/releases/X11R7.5/doc/man/man3/XSync.3.html) to make sure the event is pushed out.


```c
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
```

### Step 3.3 (XdndDrop)
Before we handle the event, make sure the version is correct.

XdndDrop occurs when the item has been dropped.

```c
if (E.xclient.message_type = XdndDrop && version <= 5) {
```

First, we should make sure we registered a valid format earlier. 

```c
    if (format) {
```

Now we can use [XConvertSection](https://tronche.com/gui/x/xlib/window-information/XConvertSelection.html) to request that the selection be converted to the format. 

We will get the result in a `SelectionNotify` event.

```c
        // newer versions of xDnD require us to tell the source our time 
        Time time = CurrentTime;
        if (version >= 1)
            time = E.xclient.data.l[2];

        XConvertSelection((Display*) display,
            XdndSelection,
            format,
            XdndSelection,
            (Window) window,
            time);
    } 
```

Otherwise, there is no drop data and the drop has ended. XDnD versions 2 and older require the target to tell the source when the drop has ended.

This can be done by sending out a `ClientMessage` event with the `XdndFinished` message type.

```c
    else if (version >= 2) {
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
```

# Step 4 (Get XDnD drop data via ClientMessage and end the interaction)
Now we can receive the converted selection from the `SlectionNotify` event

```c
case SelectionNotify: {
```

To do this, first, ensure the property is the XdndSelection. 

```c
/* this is only for checking for drops */

if (E.xselection.property != XdndSelection)
    break;
```

Now, `XGetWindowpropery` can be used to get the selection data.

```c
char* data;
unsigned long result;

Atom actualType;
int32_t actualFormat;
unsigned long bytesAfter;

XGetWindowProperty((Display*) display, E.xselection.requestor, E.xselection.property, \
                                    0, LONG_MAX, False, E.xselection.target, &actualType, 
                                    &actualFormat, &result, &bytesAfter, 
                                    (unsigned char**) &data);

if (result == 0)
    break;

printf("File dropped: %s\n", data);
```

This is the raw string data for the drop. If there are multiple drops, it will include the files separated by a '\n'. If you'd prefer an array of strings, you'd have to parse the data into an array.

The data should also be freed once you're done using it. 

If you want to use the data after the event has been processed, you should allocate a separate buffer and copy the data over.

```c
if (data)
    XFree(data);
```

the drop has ended and XDnD versions 2 and older require the target to tell the source when the drop has ended.
This can be done by sending out a `ClientMessage` event with the `XdndFinished` message type.

```c
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
```

## Full code example
```c
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
```
