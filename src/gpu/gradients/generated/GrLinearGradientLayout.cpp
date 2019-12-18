/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**************************************************************************************************
 *** This file was autogenerated from GrLinearGradientLayout.fp; do not modify.
 **************************************************************************************************/
#include "GrLinearGradientLayout.h"

#include "include/gpu/GrTexture.h"
#include "src/gpu/glsl/GrGLSLFragmentProcessor.h"
#include "src/gpu/glsl/GrGLSLFragmentShaderBuilder.h"
#include "src/gpu/glsl/GrGLSLProgramBuilder.h"
#include "src/sksl/SkSLCPP.h"
#include "src/sksl/SkSLUtil.h"
class GrGLSLLinearGradientLayout : public GrGLSLFragmentProcessor {
public:
    GrGLSLLinearGradientLayout() {}
    void emitCode(EmitArgs& args) override {
        GrGLSLFPFragmentBuilder* fragBuilder = args.fFragBuilder;
        const GrLinearGradientLayout& _outer = args.fFp.cast<GrLinearGradientLayout>();
        (void)_outer;
        auto gradientMatrix = _outer.gradientMatrix;
        (void)gradientMatrix;
        SkString sk_TransformedCoords2D_0 =
                fragBuilder->ensureCoords2D(args.fTransformedCoords[0].fVaryingPoint);
        fragBuilder->codeAppendf(
                "half t = half(%s.x) + 9.9999997473787516e-06;\n%s = half4(t, 1.0, 0.0, 0.0);\n",
                sk_TransformedCoords2D_0.c_str(), args.fOutputColor);
    }

private:
    void onSetData(const GrGLSLProgramDataManager& pdman,
                   const GrFragmentProcessor& _proc) override {}
};
GrGLSLFragmentProcessor* GrLinearGradientLayout::onCreateGLSLInstance() const {
    return new GrGLSLLinearGradientLayout();
}
void GrLinearGradientLayout::onGetGLSLProcessorKey(const GrShaderCaps& caps,
                                                   GrProcessorKeyBuilder* b) const {}
bool GrLinearGradientLayout::onIsEqual(const GrFragmentProcessor& other) const {
    const GrLinearGradientLayout& that = other.cast<GrLinearGradientLayout>();
    (void)that;
    if (gradientMatrix != that.gradientMatrix) return false;
    return true;
}
GrLinearGradientLayout::GrLinearGradientLayout(const GrLinearGradientLayout& src)
        : INHERITED(kGrLinearGradientLayout_ClassID, src.optimizationFlags())
        , fCoordTransform0(src.fCoordTransform0)
        , gradientMatrix(src.gradientMatrix) {
    this->addCoordTransform(&fCoordTransform0);
}
std::unique_ptr<GrFragmentProcessor> GrLinearGradientLayout::clone() const {
    return std::unique_ptr<GrFragmentProcessor>(new GrLinearGradientLayout(*this));
}
GR_DEFINE_FRAGMENT_PROCESSOR_TEST(GrLinearGradientLayout);
#if GR_TEST_UTILS
std::unique_ptr<GrFragmentProcessor> GrLinearGradientLayout::TestCreate(GrProcessorTestData* d) {
    SkScalar scale = GrGradientShader::RandomParams::kGradientScale;
    SkPoint points[] = {
            {d->fRandom->nextRangeScalar(0.0f, scale), d->fRandom->nextRangeScalar(0.0f, scale)},
            {d->fRandom->nextRangeScalar(0.0f, scale), d->fRandom->nextRangeScalar(0.0f, scale)}};

    GrGradientShader::RandomParams params(d->fRandom);
    auto shader = params.fUseColors4f
                          ? SkGradientShader::MakeLinear(points, params.fColors4f,
                                                         params.fColorSpace, params.fStops,
                                                         params.fColorCount, params.fTileMode)
                          : SkGradientShader::MakeLinear(points, params.fColors, params.fStops,
                                                         params.fColorCount, params.fTileMode);
    GrTest::TestAsFPArgs asFPArgs(d);
    std::unique_ptr<GrFragmentProcessor> fp = as_SB(shader)->asFragmentProcessor(asFPArgs.args());
    SkASSERT_RELEASE(fp);
    return fp;
}
#endif

std::unique_ptr<GrFragmentProcessor> GrLinearGradientLayout::Make(const SkLinearGradient& grad,
                                                                  const GrFPArgs& args) {
    SkMatrix matrix;
    if (!grad.totalLocalMatrix(args.fPreLocalMatrix, args.fPostLocalMatrix)->invert(&matrix)) {
        return nullptr;
    }
    matrix.postConcat(grad.getGradientMatrix());
    return std::unique_ptr<GrFragmentProcessor>(new GrLinearGradientLayout(matrix));
}
