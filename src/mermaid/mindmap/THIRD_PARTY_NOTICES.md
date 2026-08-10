# Mindmap layout algorithm notices

The CoSE-Bilkent implementation in this directory is derived from these
MIT-licensed projects:

- layout-base 2.0.1, Copyright (c) 2019 iVis@Bilkent
- cose-base 2.2.0, Copyright (c) 2019-present iVis@Bilkent
- cytoscape-cose-bilkent 4.1.0, Copyright (c) 2016-2018 The Cytoscape Consortium

MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## fdlibm trigonometric compatibility

The bounded sine/cosine compatibility routines in `MindmapCoseLayout.cpp`
are adapted from fdlibm as maintained by the V8 and Chromium projects:

Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.

Developed at SunSoft, a Sun Microsystems, Inc. business.
Permission to use, copy, modify, and distribute this software is freely
granted, provided that this notice is preserved.

The original fdlibm source was modified significantly by Google Inc.
Copyright 2016 the V8 project authors. All rights reserved.
Copyright 2020 The Chromium Authors.
