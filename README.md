# RGFW Under the Hood: X11 Drag 'n Drop

## Introduction
In order to handle Drag 'n Drop events through with X11, you must use the XDnD protocall. Well the XDnD protocall is significantly more complicated than other Drag 'n Drop APIs, it's still relatively simple in theory. Although the theory is simple, implementing is very tedious. This is because you must properly comminucate with the X11 server and the source window in order to get your desired results.

This tutorial attemps to explain how to handle the XDnD protocall and manage X11 Drag 'n Drop events. The code in this tutorial is based on RGFW's [source code](https://github.com/ColleagueRiley/RGFW).

## Overview
A detailed overview of the steps required:

Firt, [X11 Atoms](https://tronche.com/gui/x/xlib/window-information/properties-and-atoms.html) will be initalized. X11 Atoms are used to ask for or send specific data or properties through X11. 
Then, the window's properties will be changed, allowing it to be aware of [XDND](https://freedesktop.org/wiki/Specifications/XDND/) (X Drag 'n Drop) events. 
When a drop happens, the window will recieve a [`ClientMessage`](https://www.x.org/releases/X11R7.5/doc/man/man3/XClientMessageEvent.3.html) Event which includes a `XdndEnter` telling the target window that the a has started.
While the drop is in progress, the source window will send updates about the drop to the target window via ClientMessage events. Each time the target window gets a update, it must confirm it recieved the update otherwise the interaction will end. 
Once the drop actually happens, the target window will recieve a [`SelectionNotify`](https://www.x.org/releases/X11R7.5/doc/man/man3/XSelectionEvent.3.html) event. 
The target window will handle this event, convert the data to a readable string and then send a ClientMessage with the `XdndFinished` atom to tell the source window that the interaction is done. 

A quick overview of the steps required:

1) Define X11 Atoms
2) Enable XDnD events for the window
3) Handle XDnD events via `ClientMessage`
4) Get XDnD drop data via `ClientMessage` and end the interaction

# Step 1 (Define X11 Atoms)
In order to handle XDnD events, XDnD atoms must be initalized via [`XInternAtom`](https://www.x.org/releases/X11R7.5/doc/man/man3/XInternAtom.3.html). These Atoms are used for sending or requesting specific data or actions. 

`XdndTypeList` is used when the target window wants to know the data types the source widnow has supports. 
`XdndSelection` is used to to examine the data during a drag and to retrive the data after a drop. This is used by both the source and target window.

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

In order to recieve XDnD events, the window must enable the `XDndAware` atom. This atom tells the window manager and the source window that the window wants to recieve XDnD events.

This can be done by creating an `XdndAware` atom and using [`XChangeProperty`](https://tronche.com/gui/x/xlib/window-information/XChangeProperty.html) to change the window's `XdndAware` property.

You also must set the XDnD version using a pointer, version 5 should be used as it is the newest version of the XDnD protocall.

```c
const Atom XdndAware = XInternAtom(display, "XdndAware", False);
const char version = 5;

XChangeProperty(display, window, XdndAware, 4, 32, PropModeReplace, &version, 1);
```

# Step 3 (Handle XDnD events via ClientMessage)
Before any events are handled, some variables must be defined. 
These variables will be given to us by the source window and will be used across multiple instances.  

These variables are, the source (of the drop), the XDnD Protocall version used and the format of the drop data.

```c
int64_t source, version;
int32_t format;
```

Now the `ClientMessage` event can be handled.

```c
case ClientMessage:
```

### Step 3.1 (XdndEnter)

```c
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
```

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

```c
    uint32_t i;
    for (i = 0; i < (uint32_t)count; i++) {
        char* name = XGetAtomName((Display*) display, formats[i]);

        char* links[2] = { (char*) (const char*) "text/uri-list", (char*) (const char*) "text/plain" };
        for (; 1; name++) {
            uint32_t j;
            for (j = 0; j < 2; j++) {
                if (*links[j] != *name) {
                    links[j] = (char*) (const char*) "\1";
                    continue;
                }

                if (*links[j] == '\0' && *name == '\0')
                    format = formats[i];

                if (*links[j] != '\0' && *links[j] != '\1')
                    links[j]++;
            }

            if (*name == '\0')
                break;
        }
    }

    if (list) {
        XFree(formats);
    }

    break;
}
```

### Step 3.2 (XdndPosition)

```c
if (E.xclient.message_type == XdndPosition && version <= 5)) {
    const int32_t xabs = (E.xclient.data.l[2] >> 16) & 0xffff;
    const int32_t yabs = (E.xclient.data.l[2]) & 0xffff;
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

```c
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
    } 
```

Else, there is no drop data and the drop has ended. XDnD versions 2 and older require the target explictially tell the source when the drop has ended.

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
```c
case SelectionNotify: {
```

```c
/* this is only for checking for drops */

if (E.xselection.property != XdndSelection)
    break;

char* data;
unsigned long result;

Atom actualType;
int32_t actualFormat;
unsigned long bytesAfter;

XGetWindowProperty((Display*) display, E.xselection.requestor, E.xselection.property, 0, LONG_MAX, False, E.xselection.target, &actualType, &actualFormat, &result, &bytesAfter, (unsigned char**) &data);

if (result == 0)
    break;
```

```c
const char* prefix = (const char*)"file://";

char* line;

while ((line = strtok(data, "\r\n"))) {
    char path[MAX_PATH];

    data = NULL;

    if (line[0] == '#')
        continue;

    char* l;
    for (l = line; 1; l++) {
        if ((l - line) > 7)
            break;
        else if (*l != prefix[(l - line)])
            break;
        else if (*l == '\0' && prefix[(l - line)] == '\0') {
            line += 7;
            while (*line != '/')
                line++;
            break;
        } else if (*l == '\0')
            break;
    }

    size_t index = 0;
    while (*line) {
        if (line[0] == '%' && line[1] && line[2]) {
            const char digits[3] = { line[1], line[2], '\0' };
            path[index] = (char) strtol(digits, NULL, 16);
            line += 2;
        } else
            path[index] = *line;

        index++;
        line++;
    }
    path[index] = '\0';
        
    printf("File dropped: %s\n", path);
}

if (data)
    XFree(data);
```

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
