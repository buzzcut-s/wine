/*
 * Copyright (C) 2007 Google (Evan Stade)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>
#include <math.h>

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "wingdi.h"
#include "gdiplus.h"
#include "gdiplus_private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(gdiplus);

/* looks-right constants */
#define TENSION_CONST (0.3)
#define ANCHOR_WIDTH (2.0)
#define MAX_ITERS (50)

/* Converts angle (in degrees) to x/y coordinates */
static void deg2xy(REAL angle, REAL x_0, REAL y_0, REAL *x, REAL *y)
{
    REAL radAngle, hypotenuse;

    radAngle = deg2rad(angle);
    hypotenuse = 50.0; /* arbitrary */

    *x = x_0 + cos(radAngle) * hypotenuse;
    *y = y_0 + sin(radAngle) * hypotenuse;
}

/* Converts from gdiplus path point type to gdi path point type. */
static BYTE convert_path_point_type(BYTE type)
{
    BYTE ret;

    switch(type & PathPointTypePathTypeMask){
        case PathPointTypeBezier:
            ret = PT_BEZIERTO;
            break;
        case PathPointTypeLine:
            ret = PT_LINETO;
            break;
        case PathPointTypeStart:
            ret = PT_MOVETO;
            break;
        default:
            ERR("Bad point type\n");
            return 0;
    }

    if(type & PathPointTypeCloseSubpath)
        ret |= PT_CLOSEFIGURE;

    return ret;
}

/* GdipDrawPie/GdipFillPie helper function */
static GpStatus draw_pie(GpGraphics *graphics, HBRUSH gdibrush, HPEN gdipen,
    REAL x, REAL y, REAL width, REAL height, REAL startAngle, REAL sweepAngle)
{
    INT save_state;
    REAL x_0, y_0, x_1, y_1, x_2, y_2;

    if(!graphics)
        return InvalidParameter;

    save_state = SaveDC(graphics->hdc);
    EndPath(graphics->hdc);
    SelectObject(graphics->hdc, gdipen);
    SelectObject(graphics->hdc, gdibrush);

    x_0 = x + (width/2.0);
    y_0 = y + (height/2.0);

    deg2xy(startAngle+sweepAngle, x_0, y_0, &x_1, &y_1);
    deg2xy(startAngle, x_0, y_0, &x_2, &y_2);

    Pie(graphics->hdc, roundr(x), roundr(y), roundr(x+width), roundr(y+height),
        roundr(x_1), roundr(y_1), roundr(x_2), roundr(y_2));

    RestoreDC(graphics->hdc, save_state);

    return Ok;
}

/* GdipDrawCurve helper function.
 * Calculates Bezier points from cardinal spline points. */
static void calc_curve_bezier(CONST GpPointF *pts, REAL tension, REAL *x1,
    REAL *y1, REAL *x2, REAL *y2)
{
    REAL xdiff, ydiff;

    /* calculate tangent */
    xdiff = pts[2].X - pts[0].X;
    ydiff = pts[2].Y - pts[0].Y;

    /* apply tangent to get control points */
    *x1 = pts[1].X - tension * xdiff;
    *y1 = pts[1].Y - tension * ydiff;
    *x2 = pts[1].X + tension * xdiff;
    *y2 = pts[1].Y + tension * ydiff;
}

/* GdipDrawCurve helper function.
 * Calculates Bezier points from cardinal spline endpoints. */
static void calc_curve_bezier_endp(REAL xend, REAL yend, REAL xadj, REAL yadj,
    REAL tension, REAL *x, REAL *y)
{
    /* tangent at endpoints is the line from the endpoint to the adjacent point */
    *x = roundr(tension * (xadj - xend) + xend);
    *y = roundr(tension * (yadj - yend) + yend);
}

/* Draws the linecap the specified color and size on the hdc.  The linecap is in
 * direction of the line from x1, y1 to x2, y2 and is anchored on x2, y2. Probably
 * should not be called on an hdc that has a path you care about. */
static void draw_cap(HDC hdc, COLORREF color, GpLineCap cap, REAL size,
    const GpCustomLineCap *custom, REAL x1, REAL y1, REAL x2, REAL y2)
{
    HGDIOBJ oldbrush, oldpen;
    GpMatrix *matrix = NULL;
    HBRUSH brush;
    HPEN pen;
    PointF *custptf = NULL;
    POINT pt[4], *custpt = NULL;
    BYTE *tp = NULL;
    REAL theta, dsmall, dbig, dx, dy;
    INT i, count;
    LOGBRUSH lb;

    if((x1 == x2) && (y1 == y2))
        return;

    theta = gdiplus_atan2(y2 - y1, x2 - x1);

    brush = CreateSolidBrush(color);
    lb.lbStyle = BS_SOLID;
    lb.lbColor = color;
    lb.lbHatch = 0;
    pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT,
               ((cap == LineCapCustom) && custom && (custom->fill)) ? size : 1,
               &lb, 0, NULL);
    oldbrush = SelectObject(hdc, brush);
    oldpen = SelectObject(hdc, pen);

    switch(cap){
        case LineCapFlat:
            break;
        case LineCapSquare:
        case LineCapSquareAnchor:
        case LineCapDiamondAnchor:
            size = size * (cap & LineCapNoAnchor ? ANCHOR_WIDTH : 1.0) / 2.0;
            if(cap == LineCapDiamondAnchor){
                dsmall = cos(theta + M_PI_2) * size;
                dbig = sin(theta + M_PI_2) * size;
            }
            else{
                dsmall = cos(theta + M_PI_4) * size;
                dbig = sin(theta + M_PI_4) * size;
            }

            /* calculating the latter points from the earlier points makes them
             * look a little better because of rounding issues */
            pt[0].x = roundr(x2 - dsmall);
            pt[1].x = roundr(((REAL)pt[0].x) + dbig + dsmall);

            pt[0].y = roundr(y2 - dbig);
            pt[3].y = roundr(((REAL)pt[0].y) + dsmall + dbig);

            pt[1].y = roundr(y2 - dsmall);
            pt[2].y = roundr(dbig + dsmall + ((REAL)pt[1].y));

            pt[3].x = roundr(x2 - dbig);
            pt[2].x = roundr(((REAL)pt[3].x) + dsmall + dbig);

            Polygon(hdc, pt, 4);

            break;
        case LineCapArrowAnchor:
            size = size * 4.0 / sqrt(3.0);

            dx = cos(M_PI / 6.0 + theta) * size;
            dy = sin(M_PI / 6.0 + theta) * size;

            pt[0].x = roundr(x2 - dx);
            pt[0].y = roundr(y2 - dy);

            dx = cos(- M_PI / 6.0 + theta) * size;
            dy = sin(- M_PI / 6.0 + theta) * size;

            pt[1].x = roundr(x2 - dx);
            pt[1].y = roundr(y2 - dy);

            pt[2].x = roundr(x2);
            pt[2].y = roundr(y2);

            Polygon(hdc, pt, 3);

            break;
        case LineCapRoundAnchor:
            dx = dy = ANCHOR_WIDTH * size / 2.0;

            x2 = (REAL) roundr(x2 - dx);
            y2 = (REAL) roundr(y2 - dy);

            Ellipse(hdc, (INT) x2, (INT) y2, roundr(x2 + 2.0 * dx),
                roundr(y2 + 2.0 * dy));
            break;
        case LineCapTriangle:
            size = size / 2.0;
            dx = cos(M_PI_2 + theta) * size;
            dy = sin(M_PI_2 + theta) * size;

            /* Using roundr here can make the triangle float off the end of the
             * line. */
            pt[0].x = ((x2 - x1) >= 0 ? floorf(x2 - dx) : ceilf(x2 - dx));
            pt[0].y = ((y2 - y1) >= 0 ? floorf(y2 - dy) : ceilf(y2 - dy));
            pt[1].x = roundr(pt[0].x + 2.0 * dx);
            pt[1].y = roundr(pt[0].y + 2.0 * dy);

            dx = cos(theta) * size;
            dy = sin(theta) * size;

            pt[2].x = roundr(x2 + dx);
            pt[2].y = roundr(y2 + dy);

            Polygon(hdc, pt, 3);

            break;
        case LineCapRound:
            dx = -cos(M_PI_2 + theta) * size;
            dy = -sin(M_PI_2 + theta) * size;

            pt[0].x = ((x2 - x1) >= 0 ? floorf(x2 - dx) : ceilf(x2 - dx));
            pt[0].y = ((y2 - y1) >= 0 ? floorf(y2 - dy) : ceilf(y2 - dy));
            pt[1].x = roundr(pt[0].x + 2.0 * dx);
            pt[1].y = roundr(pt[0].y + 2.0 * dy);

            dx = dy = size / 2.0;

            x2 = (REAL) roundr(x2 - dx);
            y2 = (REAL) roundr(y2 - dy);

            Pie(hdc, (INT) x2, (INT) y2, roundr(x2 + 2.0 * dx),
                roundr(y2 + 2.0 * dy), pt[0].x, pt[0].y, pt[1].x, pt[1].y);
            break;
        case LineCapCustom:
            if(!custom)
                break;

            count = custom->pathdata.Count;
            custptf = GdipAlloc(count * sizeof(PointF));
            custpt = GdipAlloc(count * sizeof(POINT));
            tp = GdipAlloc(count);

            if(!custptf || !custpt || !tp || (GdipCreateMatrix(&matrix) != Ok))
                goto custend;

            memcpy(custptf, custom->pathdata.Points, count * sizeof(PointF));

            GdipScaleMatrix(matrix, size, size, MatrixOrderAppend);
            GdipRotateMatrix(matrix, (180.0 / M_PI) * (theta - M_PI_2),
                             MatrixOrderAppend);
            GdipTranslateMatrix(matrix, x2, y2, MatrixOrderAppend);
            GdipTransformMatrixPoints(matrix, custptf, count);

            for(i = 0; i < count; i++){
                custpt[i].x = roundr(custptf[i].X);
                custpt[i].y = roundr(custptf[i].Y);
                tp[i] = convert_path_point_type(custom->pathdata.Types[i]);
            }

            if(custom->fill){
                BeginPath(hdc);
                PolyDraw(hdc, custpt, tp, count);
                EndPath(hdc);
                StrokeAndFillPath(hdc);
            }
            else
                PolyDraw(hdc, custpt, tp, count);

custend:
            GdipFree(custptf);
            GdipFree(custpt);
            GdipFree(tp);
            GdipDeleteMatrix(matrix);
            break;
        default:
            break;
    }

    SelectObject(hdc, oldbrush);
    SelectObject(hdc, oldpen);
    DeleteObject(brush);
    DeleteObject(pen);
}

/* Shortens the line by the given percent by changing x2, y2.
 * If percent is > 1.0 then the line will change direction.
 * If percent is negative it can lengthen the line. */
static void shorten_line_percent(REAL x1, REAL  y1, REAL *x2, REAL *y2, REAL percent)
{
    REAL dist, theta, dx, dy;

    if((y1 == *y2) && (x1 == *x2))
        return;

    dist = sqrt((*x2 - x1) * (*x2 - x1) + (*y2 - y1) * (*y2 - y1)) * -percent;
    theta = gdiplus_atan2((*y2 - y1), (*x2 - x1));
    dx = cos(theta) * dist;
    dy = sin(theta) * dist;

    *x2 = *x2 + dx;
    *y2 = *y2 + dy;
}

/* Shortens the line by the given amount by changing x2, y2.
 * If the amount is greater than the distance, the line will become length 0.
 * If the amount is negative, it can lengthen the line. */
static void shorten_line_amt(REAL x1, REAL y1, REAL *x2, REAL *y2, REAL amt)
{
    REAL dx, dy, percent;

    dx = *x2 - x1;
    dy = *y2 - y1;
    if(dx == 0 && dy == 0)
        return;

    percent = amt / sqrt(dx * dx + dy * dy);
    if(percent >= 1.0){
        *x2 = x1;
        *y2 = y1;
        return;
    }

    shorten_line_percent(x1, y1, x2, y2, percent);
}

/* Draws lines between the given points, and if caps is true then draws an endcap
 * at the end of the last line.  FIXME: Startcaps not implemented. */
static GpStatus draw_polyline(HDC hdc, GpPen *pen, GDIPCONST GpPointF * pt,
    INT count, BOOL caps)
{
    POINT *pti = NULL;
    GpPointF *ptcopy = NULL;
    INT i;
    GpStatus status = GenericError;

    if(!count)
        return Ok;

    pti = GdipAlloc(count * sizeof(POINT));
    ptcopy = GdipAlloc(count * sizeof(GpPointF));

    if(!pti || !ptcopy){
        status = OutOfMemory;
        goto end;
    }

    memcpy(ptcopy, pt, count * sizeof(GpPointF));

    if(caps){
        if(pen->endcap == LineCapArrowAnchor)
            shorten_line_amt(ptcopy[count-2].X, ptcopy[count-2].Y,
                             &ptcopy[count-1].X, &ptcopy[count-1].Y, pen->width);
        else if((pen->endcap == LineCapCustom) && pen->customend)
            shorten_line_amt(ptcopy[count-2].X, ptcopy[count-2].Y,
                             &ptcopy[count-1].X, &ptcopy[count-1].Y,
                             pen->customend->inset * pen->width);

        if(pen->startcap == LineCapArrowAnchor)
            shorten_line_amt(ptcopy[1].X, ptcopy[1].Y,
                             &ptcopy[0].X, &ptcopy[0].Y, pen->width);
        else if((pen->startcap == LineCapCustom) && pen->customstart)
            shorten_line_amt(ptcopy[1].X, ptcopy[1].Y,
                             &ptcopy[0].X, &ptcopy[0].Y,
                             pen->customend->inset * pen->width);

        draw_cap(hdc, pen->color, pen->endcap, pen->width, pen->customend,
                 pt[count - 2].X, pt[count - 2].Y, pt[count - 1].X, pt[count - 1].Y);
        draw_cap(hdc, pen->color, pen->startcap, pen->width, pen->customstart,
                         pt[1].X, pt[1].Y, pt[0].X, pt[0].Y);
    }

    for(i = 0; i < count; i ++){
        pti[i].x = roundr(ptcopy[i].X);
        pti[i].y = roundr(ptcopy[i].Y);
    }

    Polyline(hdc, pti, count);

end:
    GdipFree(pti);
    GdipFree(ptcopy);

    return status;
}

/* Conducts a linear search to find the bezier points that will back off
 * the endpoint of the curve by a distance of amt. Linear search works
 * better than binary in this case because there are multiple solutions,
 * and binary searches often find a bad one. I don't think this is what
 * Windows does but short of rendering the bezier without GDI's help it's
 * the best we can do. If rev then work from the start of the passed points
 * instead of the end. */
static void shorten_bezier_amt(GpPointF * pt, REAL amt, BOOL rev)
{
    GpPointF origpt[4];
    REAL percent = 0.00, dx, dy, origx, origy, diff = -1.0;
    INT i, first = 0, second = 1, third = 2, fourth = 3;

    if(rev){
        first = 3;
        second = 2;
        third = 1;
        fourth = 0;
    }

    origx = pt[fourth].X;
    origy = pt[fourth].Y;
    memcpy(origpt, pt, sizeof(GpPointF) * 4);

    for(i = 0; (i < MAX_ITERS) && (diff < amt); i++){
        /* reset bezier points to original values */
        memcpy(pt, origpt, sizeof(GpPointF) * 4);
        /* Perform magic on bezier points. Order is important here.*/
        shorten_line_percent(pt[third].X, pt[third].Y, &pt[fourth].X, &pt[fourth].Y, percent);
        shorten_line_percent(pt[second].X, pt[second].Y, &pt[third].X, &pt[third].Y, percent);
        shorten_line_percent(pt[third].X, pt[third].Y, &pt[fourth].X, &pt[fourth].Y, percent);
        shorten_line_percent(pt[first].X, pt[first].Y, &pt[second].X, &pt[second].Y, percent);
        shorten_line_percent(pt[second].X, pt[second].Y, &pt[third].X, &pt[third].Y, percent);
        shorten_line_percent(pt[third].X, pt[third].Y, &pt[fourth].X, &pt[fourth].Y, percent);

        dx = pt[fourth].X - origx;
        dy = pt[fourth].Y - origy;

        diff = sqrt(dx * dx + dy * dy);
        percent += 0.0005 * amt;
    }
}

/* Draws bezier curves between given points, and if caps is true then draws an
 * endcap at the end of the last line.  FIXME: Startcaps not implemented. */
static GpStatus draw_polybezier(HDC hdc, GpPen *pen, GDIPCONST GpPointF * pt,
    INT count, BOOL caps)
{
    POINT *pti, curpos;
    GpPointF *ptcopy;
    INT i;
    REAL x, y;
    GpStatus status = GenericError;

    if(!count)
        return Ok;

    pti = GdipAlloc(count * sizeof(POINT));
    ptcopy = GdipAlloc(count * sizeof(GpPointF));

    if(!pti || !ptcopy){
        status = OutOfMemory;
        goto end;
    }

    memcpy(ptcopy, pt, count * sizeof(GpPointF));

    if(caps){
        if(pen->endcap == LineCapArrowAnchor)
            shorten_bezier_amt(&ptcopy[count-4], pen->width, FALSE);
        /* FIXME The following is seemingly correct only for baseinset < 0 or
         * baseinset > ~3. With smaller baseinsets, windows actually
         * lengthens the bezier line instead of shortening it. */
        else if((pen->endcap == LineCapCustom) && pen->customend){
            x = pt[count - 1].X;
            y = pt[count - 1].Y;
            shorten_line_amt(pt[count - 2].X, pt[count - 2].Y, &x, &y,
                             pen->width * pen->customend->inset);
            MoveToEx(hdc, roundr(pt[count - 1].X), roundr(pt[count - 1].Y), &curpos);
            LineTo(hdc, roundr(x), roundr(y));
            MoveToEx(hdc, curpos.x, curpos.y, NULL);
        }

        if(pen->startcap == LineCapArrowAnchor)
            shorten_bezier_amt(ptcopy, pen->width, TRUE);
        else if((pen->startcap == LineCapCustom) && pen->customstart){
            x = ptcopy[0].X;
            y = ptcopy[0].Y;
            shorten_line_amt(ptcopy[1].X, ptcopy[1].Y, &x, &y,
                             pen->width * pen->customend->inset);
            MoveToEx(hdc, roundr(pt[0].X), roundr(pt[0].Y), &curpos);
            LineTo(hdc, roundr(x), roundr(y));
            MoveToEx(hdc, curpos.x, curpos.y, NULL);
        }

        /* the direction of the line cap is parallel to the direction at the
         * end of the bezier (which, if it has been shortened, is not the same
         * as the direction from pt[count-2] to pt[count-1]) */
        draw_cap(hdc, pen->color, pen->endcap, pen->width, pen->customend,
            pt[count - 1].X - (ptcopy[count - 1].X - ptcopy[count - 2].X),
            pt[count - 1].Y - (ptcopy[count - 1].Y - ptcopy[count - 2].Y),
            pt[count - 1].X, pt[count - 1].Y);

        draw_cap(hdc, pen->color, pen->startcap, pen->width, pen->customstart,
            pt[0].X - (ptcopy[0].X - ptcopy[1].X),
            pt[0].Y - (ptcopy[0].Y - ptcopy[1].Y), pt[0].X, pt[0].Y);
    }

    for(i = 0; i < count; i ++){
        pti[i].x = roundr(ptcopy[i].X);
        pti[i].y = roundr(ptcopy[i].Y);
    }

    PolyBezier(hdc, pti, count);

    status = Ok;

end:
    GdipFree(pti);
    GdipFree(ptcopy);

    return status;
}

/* Draws a combination of bezier curves and lines between points. */
static GpStatus draw_poly(HDC hdc, GpPen *pen, GDIPCONST GpPointF * pt,
    GDIPCONST BYTE * types, INT count, BOOL caps)
{
    POINT *pti = GdipAlloc(count * sizeof(POINT)), curpos;
    BYTE *tp = GdipAlloc(count);
    GpPointF *ptcopy = GdipAlloc(count * sizeof(GpPointF));
    REAL x = pt[count - 1].X, y = pt[count - 1].Y;
    INT i, j;
    GpStatus status = GenericError;

    if(!count){
        status = Ok;
        goto end;
    }
    if(!pti || !tp || !ptcopy){
        status = OutOfMemory;
        goto end;
    }

    for(i = 1; i < count; i++){
        if((types[i] & PathPointTypePathTypeMask) == PathPointTypeBezier){
            if((i + 2 >= count) || !(types[i + 1] & PathPointTypeBezier)
                || !(types[i + 1] & PathPointTypeBezier)){
                ERR("Bad bezier points\n");
                goto end;
            }
            i += 2;
        }
    }

    /* If we are drawing caps, go through the points and adjust them accordingly,
     * and draw the caps. */
    if(caps){
        memcpy(ptcopy, pt, count * sizeof(GpPointF));

        switch(types[count - 1] & PathPointTypePathTypeMask){
            case PathPointTypeBezier:
                if(pen->endcap == LineCapArrowAnchor)
                    shorten_bezier_amt(&ptcopy[count - 4], pen->width, FALSE);
                else if((pen->endcap == LineCapCustom) && pen->customend){
                    x = pt[count - 1].X;
                    y = pt[count - 1].Y;
                    shorten_line_amt(pt[count - 2].X, pt[count - 2].Y, &x, &y,
                                     pen->width * pen->customend->inset);
                    MoveToEx(hdc, roundr(pt[count - 1].X), roundr(pt[count - 1].Y), &curpos);
                    LineTo(hdc, roundr(x), roundr(y));
                    MoveToEx(hdc, curpos.x, curpos.y, NULL);
                }

                draw_cap(hdc, pen->color, pen->endcap, pen->width, pen->customend,
                    pt[count - 1].X - (ptcopy[count - 1].X - ptcopy[count - 2].X),
                    pt[count - 1].Y - (ptcopy[count - 1].Y - ptcopy[count - 2].Y),
                    pt[count - 1].X, pt[count - 1].Y);

                break;
            case PathPointTypeLine:
                if(pen->endcap == LineCapArrowAnchor)
                    shorten_line_amt(ptcopy[count - 2].X, ptcopy[count - 2].Y,
                                     &ptcopy[count - 1].X, &ptcopy[count - 1].Y,
                                     pen->width);
                else if((pen->endcap == LineCapCustom) && pen->customend)
                    shorten_line_amt(ptcopy[count - 2].X, ptcopy[count - 2].Y,
                                     &ptcopy[count - 1].X, &ptcopy[count - 1].Y,
                                     pen->customend->inset * pen->width);

                draw_cap(hdc, pen->color, pen->endcap, pen->width, pen->customend,
                         pt[count - 2].X, pt[count - 2].Y, pt[count - 1].X,
                         pt[count - 1].Y);

                break;
            default:
                ERR("Bad path last point\n");
                goto end;
        }

        /* Find start of points */
        for(j = 1; j < count && ((types[j] & PathPointTypePathTypeMask)
            == PathPointTypeStart); j++);

        switch(types[j] & PathPointTypePathTypeMask){
            case PathPointTypeBezier:
                if(pen->startcap == LineCapArrowAnchor)
                    shorten_bezier_amt(&ptcopy[j - 1], pen->width, TRUE);
                else if((pen->startcap == LineCapCustom) && pen->customstart){
                    x = pt[j - 1].X;
                    y = pt[j - 1].Y;
                    shorten_line_amt(ptcopy[j].X, ptcopy[j].Y, &x, &y,
                                     pen->width * pen->customstart->inset);
                    MoveToEx(hdc, roundr(pt[j - 1].X), roundr(pt[j - 1].Y), &curpos);
                    LineTo(hdc, roundr(x), roundr(y));
                    MoveToEx(hdc, curpos.x, curpos.y, NULL);
                }

                draw_cap(hdc, pen->color, pen->startcap, pen->width, pen->customstart,
                    pt[j - 1].X - (ptcopy[j - 1].X - ptcopy[j].X),
                    pt[j - 1].Y - (ptcopy[j - 1].Y - ptcopy[j].Y),
                    pt[j - 1].X, pt[j - 1].Y);

                break;
            case PathPointTypeLine:
                if(pen->startcap == LineCapArrowAnchor)
                    shorten_line_amt(ptcopy[j].X, ptcopy[j].Y,
                                     &ptcopy[j - 1].X, &ptcopy[j - 1].Y,
                                     pen->width);
                else if((pen->startcap == LineCapCustom) && pen->customstart)
                    shorten_line_amt(ptcopy[j].X, ptcopy[j].Y,
                                     &ptcopy[j - 1].X, &ptcopy[j - 1].Y,
                                     pen->customstart->inset * pen->width);

                draw_cap(hdc, pen->color, pen->endcap, pen->width, pen->customstart,
                         pt[j].X, pt[j].Y, pt[j - 1].X,
                         pt[j - 1].Y);

                break;
            default:
                ERR("Bad path points\n");
                goto end;
        }
        for(i = 0; i < count; i++){
            pti[i].x = roundr(ptcopy[i].X);
            pti[i].y = roundr(ptcopy[i].Y);
        }
    }
    else
        for(i = 0; i < count; i++){
            pti[i].x = roundr(pt[i].X);
            pti[i].y = roundr(pt[i].Y);
        }

    for(i = 0; i < count; i++){
        tp[i] = convert_path_point_type(types[i]);
    }

    PolyDraw(hdc, pti, tp, count);

    status = Ok;

end:
    GdipFree(pti);
    GdipFree(ptcopy);
    GdipFree(tp);

    return status;
}

GpStatus WINGDIPAPI GdipCreateFromHDC(HDC hdc, GpGraphics **graphics)
{
    if(hdc == NULL)
        return OutOfMemory;

    if(graphics == NULL)
        return InvalidParameter;

    *graphics = GdipAlloc(sizeof(GpGraphics));
    if(!*graphics)  return OutOfMemory;

    (*graphics)->hdc = hdc;
    (*graphics)->hwnd = NULL;
    (*graphics)->smoothing = SmoothingModeDefault;
    (*graphics)->compqual = CompositingQualityDefault;
    (*graphics)->interpolation = InterpolationModeDefault;
    (*graphics)->pixeloffset = PixelOffsetModeDefault;

    return Ok;
}

GpStatus WINGDIPAPI GdipCreateFromHWND(HWND hwnd, GpGraphics **graphics)
{
    GpStatus ret;

    if((ret = GdipCreateFromHDC(GetDC(hwnd), graphics)) != Ok)
        return ret;

    (*graphics)->hwnd = hwnd;

    return Ok;
}

GpStatus WINGDIPAPI GdipDeleteGraphics(GpGraphics *graphics)
{
    if(!graphics) return InvalidParameter;
    if(graphics->hwnd)
        ReleaseDC(graphics->hwnd, graphics->hdc);

    HeapFree(GetProcessHeap(), 0, graphics);

    return Ok;
}

GpStatus WINGDIPAPI GdipDrawArc(GpGraphics *graphics, GpPen *pen, REAL x,
    REAL y, REAL width, REAL height, REAL startAngle, REAL sweepAngle)
{
    INT save_state, num_pts;
    GpPointF points[MAX_ARC_PTS];
    GpStatus retval;

    if(!graphics || !pen)
        return InvalidParameter;

    num_pts = arc2polybezier(points, x, y, width, height, startAngle, sweepAngle);

    save_state = SaveDC(graphics->hdc);
    EndPath(graphics->hdc);
    SelectObject(graphics->hdc, pen->gdipen);

    retval = draw_polybezier(graphics->hdc, pen, points, num_pts, TRUE);

    RestoreDC(graphics->hdc, save_state);

    return retval;
}

GpStatus WINGDIPAPI GdipDrawBezier(GpGraphics *graphics, GpPen *pen, REAL x1,
    REAL y1, REAL x2, REAL y2, REAL x3, REAL y3, REAL x4, REAL y4)
{
    INT save_state;
    GpPointF pt[4];
    GpStatus retval;

    if(!graphics || !pen)
        return InvalidParameter;

    pt[0].X = x1;
    pt[0].Y = y1;
    pt[1].X = x2;
    pt[1].Y = y2;
    pt[2].X = x3;
    pt[2].Y = y3;
    pt[3].X = x4;
    pt[3].Y = y4;

    save_state = SaveDC(graphics->hdc);
    EndPath(graphics->hdc);
    SelectObject(graphics->hdc, pen->gdipen);

    retval = draw_polybezier(graphics->hdc, pen, pt, 4, TRUE);

    RestoreDC(graphics->hdc, save_state);

    return retval;
}

/* Approximates cardinal spline with Bezier curves. */
GpStatus WINGDIPAPI GdipDrawCurve2(GpGraphics *graphics, GpPen *pen,
    GDIPCONST GpPointF *points, INT count, REAL tension)
{
    /* PolyBezier expects count*3-2 points. */
    INT i, len_pt = count*3-2, save_state;
    GpPointF *pt;
    REAL x1, x2, y1, y2;
    GpStatus retval;

    if(!graphics || !pen)
        return InvalidParameter;

    pt = GdipAlloc(len_pt * sizeof(GpPointF));
    tension = tension * TENSION_CONST;

    calc_curve_bezier_endp(points[0].X, points[0].Y, points[1].X, points[1].Y,
        tension, &x1, &y1);

    pt[0].X = points[0].X;
    pt[0].Y = points[0].Y;
    pt[1].X = x1;
    pt[1].Y = y1;

    for(i = 0; i < count-2; i++){
        calc_curve_bezier(&(points[i]), tension, &x1, &y1, &x2, &y2);

        pt[3*i+2].X = x1;
        pt[3*i+2].Y = y1;
        pt[3*i+3].X = points[i+1].X;
        pt[3*i+3].Y = points[i+1].Y;
        pt[3*i+4].X = x2;
        pt[3*i+4].Y = y2;
    }

    calc_curve_bezier_endp(points[count-1].X, points[count-1].Y,
        points[count-2].X, points[count-2].Y, tension, &x1, &y1);

    pt[len_pt-2].X = x1;
    pt[len_pt-2].Y = y1;
    pt[len_pt-1].X = points[count-1].X;
    pt[len_pt-1].Y = points[count-1].Y;

    save_state = SaveDC(graphics->hdc);
    EndPath(graphics->hdc);
    SelectObject(graphics->hdc, pen->gdipen);

    retval = draw_polybezier(graphics->hdc, pen, pt, len_pt, TRUE);

    GdipFree(pt);
    RestoreDC(graphics->hdc, save_state);

    return retval;
}

GpStatus WINGDIPAPI GdipDrawLineI(GpGraphics *graphics, GpPen *pen, INT x1,
    INT y1, INT x2, INT y2)
{
    INT save_state;
    GpPointF pt[2];
    GpStatus retval;

    if(!pen || !graphics)
        return InvalidParameter;

    pt[0].X = (REAL)x1;
    pt[0].Y = (REAL)y1;
    pt[1].X = (REAL)x2;
    pt[1].Y = (REAL)y2;

    save_state = SaveDC(graphics->hdc);
    EndPath(graphics->hdc);
    SelectObject(graphics->hdc, pen->gdipen);

    retval = draw_polyline(graphics->hdc, pen, pt, 2, TRUE);

    RestoreDC(graphics->hdc, save_state);

    return retval;
}

GpStatus WINGDIPAPI GdipDrawLines(GpGraphics *graphics, GpPen *pen, GDIPCONST
    GpPointF *points, INT count)
{
    INT save_state;
    GpStatus retval;

    if(!pen || !graphics || (count < 2))
        return InvalidParameter;

    save_state = SaveDC(graphics->hdc);
    EndPath(graphics->hdc);
    SelectObject(graphics->hdc, pen->gdipen);

    retval = draw_polyline(graphics->hdc, pen, points, count, TRUE);

    RestoreDC(graphics->hdc, save_state);

    return retval;
}

GpStatus WINGDIPAPI GdipDrawPath(GpGraphics *graphics, GpPen *pen, GpPath *path)
{
    INT save_state;
    GpStatus retval;

    if(!pen || !graphics)
        return InvalidParameter;

    save_state = SaveDC(graphics->hdc);
    EndPath(graphics->hdc);
    SelectObject(graphics->hdc, pen->gdipen);

    retval = draw_poly(graphics->hdc, pen, path->pathdata.Points,
                       path->pathdata.Types, path->pathdata.Count, TRUE);

    RestoreDC(graphics->hdc, save_state);

    return retval;
}

GpStatus WINGDIPAPI GdipDrawPie(GpGraphics *graphics, GpPen *pen, REAL x,
    REAL y, REAL width, REAL height, REAL startAngle, REAL sweepAngle)
{
    if(!pen)
        return InvalidParameter;

    return draw_pie(graphics, GetStockObject(NULL_BRUSH), pen->gdipen, x, y,
        width, height, startAngle, sweepAngle);
}

GpStatus WINGDIPAPI GdipDrawRectangleI(GpGraphics *graphics, GpPen *pen, INT x,
    INT y, INT width, INT height)
{
    INT save_state;

    if(!pen || !graphics)
        return InvalidParameter;

    save_state = SaveDC(graphics->hdc);
    EndPath(graphics->hdc);
    SelectObject(graphics->hdc, pen->gdipen);
    SelectObject(graphics->hdc, GetStockObject(NULL_BRUSH));

    Rectangle(graphics->hdc, x, y, x + width, y + height);

    RestoreDC(graphics->hdc, save_state);

    return Ok;
}

GpStatus WINGDIPAPI GdipFillPath(GpGraphics *graphics, GpBrush *brush, GpPath *path)
{
    INT save_state;
    GpStatus retval;

    if(!brush || !graphics || !path)
        return InvalidParameter;

    save_state = SaveDC(graphics->hdc);
    EndPath(graphics->hdc);
    SelectObject(graphics->hdc, brush->gdibrush);
    SetPolyFillMode(graphics->hdc, (path->fill == FillModeAlternate ? ALTERNATE
                                                                    : WINDING));

    BeginPath(graphics->hdc);
    retval = draw_poly(graphics->hdc, NULL, path->pathdata.Points,
                       path->pathdata.Types, path->pathdata.Count, FALSE);

    if(retval != Ok)
        goto end;

    EndPath(graphics->hdc);
    FillPath(graphics->hdc);

    retval = Ok;

end:
    RestoreDC(graphics->hdc, save_state);

    return retval;
}

GpStatus WINGDIPAPI GdipFillPie(GpGraphics *graphics, GpBrush *brush, REAL x,
    REAL y, REAL width, REAL height, REAL startAngle, REAL sweepAngle)
{
    if(!brush)
        return InvalidParameter;

    return draw_pie(graphics, brush->gdibrush, GetStockObject(NULL_PEN), x, y,
        width, height, startAngle, sweepAngle);
}

/* FIXME: Compositing quality is not used anywhere except the getter/setter. */
GpStatus WINGDIPAPI GdipGetCompositingQuality(GpGraphics *graphics,
    CompositingQuality *quality)
{
    if(!graphics || !quality)
        return InvalidParameter;

    *quality = graphics->compqual;

    return Ok;
}

/* FIXME: Interpolation mode is not used anywhere except the getter/setter. */
GpStatus WINGDIPAPI GdipGetInterpolationMode(GpGraphics *graphics,
    InterpolationMode *mode)
{
    if(!graphics || !mode)
        return InvalidParameter;

    *mode = graphics->interpolation;

    return Ok;
}

/* FIXME: Pixel offset mode is not used anywhere except the getter/setter. */
GpStatus WINGDIPAPI GdipGetPixelOffsetMode(GpGraphics *graphics, PixelOffsetMode
    *mode)
{
    if(!graphics || !mode)
        return InvalidParameter;

    *mode = graphics->pixeloffset;

    return Ok;
}

/* FIXME: Smoothing mode is not used anywhere except the getter/setter. */
GpStatus WINGDIPAPI GdipGetSmoothingMode(GpGraphics *graphics, SmoothingMode *mode)
{
    if(!graphics || !mode)
        return InvalidParameter;

    *mode = graphics->smoothing;

    return Ok;
}

GpStatus WINGDIPAPI GdipRestoreGraphics(GpGraphics *graphics, GraphicsState state)
{
    if(!graphics)
        return InvalidParameter;

    FIXME("graphics state not implemented\n");

    return NotImplemented;
}

GpStatus WINGDIPAPI GdipSaveGraphics(GpGraphics *graphics, GraphicsState *state)
{
    if(!graphics || !state)
        return InvalidParameter;

    FIXME("graphics state not implemented\n");

    return NotImplemented;
}

GpStatus WINGDIPAPI GdipSetCompositingQuality(GpGraphics *graphics,
    CompositingQuality quality)
{
    if(!graphics)
        return InvalidParameter;

    graphics->compqual = quality;

    return Ok;
}

GpStatus WINGDIPAPI GdipSetInterpolationMode(GpGraphics *graphics,
    InterpolationMode mode)
{
    if(!graphics)
        return InvalidParameter;

    graphics->interpolation = mode;

    return Ok;
}

GpStatus WINGDIPAPI GdipSetPixelOffsetMode(GpGraphics *graphics, PixelOffsetMode
    mode)
{
    if(!graphics)
        return InvalidParameter;

    graphics->pixeloffset = mode;

    return Ok;
}

GpStatus WINGDIPAPI GdipSetSmoothingMode(GpGraphics *graphics, SmoothingMode mode)
{
    if(!graphics)
        return InvalidParameter;

    graphics->smoothing = mode;

    return Ok;
}
