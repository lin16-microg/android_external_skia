/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkEmbossMaskFilter.h"
#include "SkBlurMaskFilter.h"
#include "SkBlurMask.h"
#include "SkColorPriv.h"
#include "SkEmbossMask.h"
#include "SkReadBuffer.h"
#include "SkWriteBuffer.h"
#include "SkString.h"

static void normalize3(SkScalar dst[3], const SkScalar src[3]) {
    SkScalar mag = SkScalarSquare(src[0]) + SkScalarSquare(src[1]) + SkScalarSquare(src[2]);
    SkScalar scale = SkScalarInvert(SkScalarSqrt(mag));

    for (int i = 0; i < 3; i++) {
        dst[i] = src[i] * scale;
    }
}

sk_sp<SkMaskFilter> SkEmbossMaskFilter::Make(SkScalar blurSigma, const Light& light) {
    if (!SkScalarIsFinite(blurSigma) || blurSigma <= 0) {
        return nullptr;
    }

    Light newLight = light;
    normalize3(newLight.fDirection, light.fDirection);
    if (!SkScalarsAreFinite(newLight.fDirection, 3)) {
        return nullptr;
    }

    return sk_sp<SkMaskFilter>(new SkEmbossMaskFilter(blurSigma, newLight));
}

#ifdef SK_SUPPORT_LEGACY_EMBOSSMASKFILTER
sk_sp<SkMaskFilter> SkBlurMaskFilter::MakeEmboss(SkScalar blurSigma, const SkScalar direction[3],
                                                 SkScalar ambient, SkScalar specular) {
    if (direction == nullptr) {
        return nullptr;
    }

    SkEmbossMaskFilter::Light   light;

    memcpy(light.fDirection, direction, sizeof(light.fDirection));
    // ambient should be 0...1 as a scalar
    light.fAmbient = SkUnitScalarClampToByte(ambient);
    // specular should be 0..15.99 as a scalar
    static const SkScalar kSpecularMultiplier = SkIntToScalar(255) / 16;
    light.fSpecular = static_cast<U8CPU>(SkScalarPin(specular, 0, 16) * kSpecularMultiplier + 0.5);

    return SkEmbossMaskFilter::Make(blurSigma, light);
}
#endif

///////////////////////////////////////////////////////////////////////////////

SkEmbossMaskFilter::SkEmbossMaskFilter(SkScalar blurSigma, const Light& light)
    : fLight(light), fBlurSigma(blurSigma)
{
    SkASSERT(fBlurSigma > 0);
    SkASSERT(SkScalarsAreFinite(fLight.fDirection, 3));
}

SkMask::Format SkEmbossMaskFilter::getFormat() const {
    return SkMask::k3D_Format;
}

bool SkEmbossMaskFilter::filterMask(SkMask* dst, const SkMask& src,
                                    const SkMatrix& matrix, SkIPoint* margin) const {
    SkScalar sigma = matrix.mapRadius(fBlurSigma);

    if (!SkBlurMask::BoxBlur(dst, src, sigma, kInner_SkBlurStyle, kLow_SkBlurQuality)) {
        return false;
    }

    dst->fFormat = SkMask::k3D_Format;
    if (margin) {
        margin->set(SkScalarCeilToInt(3*sigma), SkScalarCeilToInt(3*sigma));
    }

    if (src.fImage == nullptr) {
        return true;
    }

    // create a larger buffer for the other two channels (should force fBlur to do this for us)

    {
        uint8_t* alphaPlane = dst->fImage;
        size_t totalSize = dst->computeTotalImageSize();
        if (totalSize == 0) {
            return false;  // too big to allocate, abort
        }
        size_t planeSize = dst->computeImageSize();
        SkASSERT(planeSize != 0);  // if totalSize didn't overflow, this can't either
        dst->fImage = SkMask::AllocImage(totalSize);
        memcpy(dst->fImage, alphaPlane, planeSize);
        SkMask::FreeImage(alphaPlane);
    }

    // run the light direction through the matrix...
    Light   light = fLight;
    matrix.mapVectors((SkVector*)(void*)light.fDirection,
                      (SkVector*)(void*)fLight.fDirection, 1);

    // now restore the length of the XY component
    // cast to SkVector so we can call setLength (this double cast silences alias warnings)
    SkVector* vec = (SkVector*)(void*)light.fDirection;
    vec->setLength(light.fDirection[0],
                   light.fDirection[1],
                   SkPoint::Length(fLight.fDirection[0], fLight.fDirection[1]));

    SkEmbossMask::Emboss(dst, light);

    // restore original alpha
    memcpy(dst->fImage, src.fImage, src.computeImageSize());

    return true;
}

sk_sp<SkFlattenable> SkEmbossMaskFilter::CreateProc(SkReadBuffer& buffer) {
    Light light;
    if (buffer.readByteArray(&light, sizeof(Light))) {
        light.fPad = 0; // for the font-cache lookup to be clean
        const SkScalar sigma = buffer.readScalar();
        return Make(sigma, light);
    }
    return nullptr;
}

void SkEmbossMaskFilter::flatten(SkWriteBuffer& buffer) const {
    Light tmpLight = fLight;
    tmpLight.fPad = 0;    // for the font-cache lookup to be clean
    buffer.writeByteArray(&tmpLight, sizeof(tmpLight));
    buffer.writeScalar(fBlurSigma);
}

#ifndef SK_IGNORE_TO_STRING
void SkEmbossMaskFilter::toString(SkString* str) const {
    str->append("SkEmbossMaskFilter: (");

    str->append("direction: (");
    str->appendScalar(fLight.fDirection[0]);
    str->append(", ");
    str->appendScalar(fLight.fDirection[1]);
    str->append(", ");
    str->appendScalar(fLight.fDirection[2]);
    str->append(") ");

    str->appendf("ambient: %d specular: %d ",
        fLight.fAmbient, fLight.fSpecular);

    str->append("blurSigma: ");
    str->appendScalar(fBlurSigma);
    str->append(")");
}
#endif
