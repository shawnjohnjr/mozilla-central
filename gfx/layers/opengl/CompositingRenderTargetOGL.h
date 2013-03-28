/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_COMPOSITINGRENDERTARGETOGL_H
#define MOZILLA_GFX_COMPOSITINGRENDERTARGETOGL_H

#include "mozilla/layers/CompositorOGL.h"
#include "mozilla/gfx/Rect.h"
#include "gfxASurface.h"

#ifdef MOZ_DUMP_PAINTING
#include "mozilla/layers/CompositorOGL.h"
#endif

namespace mozilla {
namespace gl {
  class TextureImage;
  class BindableTexture;
}
namespace layers {

class CompositingRenderTargetOGL : public CompositingRenderTarget
{
  typedef gfxASurface::gfxContentType ContentType;
  typedef mozilla::gl::GLContext GLContext;

  // For lazy initialisation of the GL stuff
  struct InitParams
  {
    InitParams() : mStatus(NO_PARAMS) {}
    InitParams(const gfx::IntRect& aRect,
               GLenum aFBOTextureTarget,
               SurfaceInitMode aInit)
      : mStatus(READY)
      , mRect(aRect)
      , mFBOTextureTarget(aFBOTextureTarget)
      , mInit(aInit)
    {}

    enum {
      NO_PARAMS,
      READY,
      INITIALIZED
    } mStatus;
    gfx::IntRect mRect;
    GLenum mFBOTextureTarget;
    SurfaceInitMode mInit;
  };

public:
  CompositingRenderTargetOGL(CompositorOGL* aCompositor, GLuint aTexure, GLuint aFBO)
    : mInitParams()
    , mCompositor(aCompositor)
    , mGL(aCompositor->gl())
    , mTextureHandle(aTexure)
    , mFBO(aFBO)
  {}

  ~CompositingRenderTargetOGL()
  {
    mGL->fDeleteTextures(1, &mTextureHandle);
    mGL->fDeleteFramebuffers(1, &mFBO);
  }

  /**
   * Some initialisation work on the backing FBO and texture.
   * We do this lazily so that when we first set this render target on the
   * compositor we do not have to re-bind the FBO after unbinding it, or
   * alternatively leave the FBO bound after creation.
   */
  void Initialize(const gfx::IntRect& aRect,
                  GLenum aFBOTextureTarget,
                  SurfaceInitMode aInit)
  {
    MOZ_ASSERT(mInitParams.mStatus == InitParams::NO_PARAMS, "Initialized twice?");
    // postpone initialization until we actually want to use this render target
    mInitParams = InitParams(aRect, aFBOTextureTarget, aInit);
  }

  void BindTexture(GLenum aTextureUnit, GLenum aTextureTarget)
  {
    MOZ_ASSERT(mInitParams.mStatus == InitParams::INITIALIZED);
    MOZ_ASSERT(mTextureHandle != 0);
    mGL->fActiveTexture(aTextureUnit);
    mGL->fBindTexture(aTextureTarget, mTextureHandle);
  }

  /**
   * Call when we want to draw into our FBO
   */
  void BindRenderTarget()
  {
    if (mInitParams.mStatus != InitParams::INITIALIZED) {
      InitializeImpl();
    } else {
      MOZ_ASSERT(mInitParams.mStatus == InitParams::INITIALIZED);
      mGL->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, mFBO);
      mCompositor->PrepareViewport(mInitParams.mRect.width,
                                   mInitParams.mRect.height,
                                   gfxMatrix());
    }
  }

  GLuint GetFBO() const
  {
    MOZ_ASSERT(mInitParams.mStatus == InitParams::INITIALIZED);
    return mFBO;
  }

  GLuint GetTextureHandle() const
  {
    MOZ_ASSERT(mInitParams.mStatus == InitParams::INITIALIZED);
    return mTextureHandle;
  }

  // TextureSourceOGL
  TextureSourceOGL* AsSourceOGL() MOZ_OVERRIDE
  {
    MOZ_ASSERT(false, "CompositingRenderTargetOGL should not be used as a TextureSource");
    return nullptr;
  }
  gfx::IntSize GetSize() const MOZ_OVERRIDE
  {
    MOZ_ASSERT(false, "CompositingRenderTargetOGL should not be used as a TextureSource");
    return gfx::IntSize(0, 0);
  }

#ifdef MOZ_DUMP_PAINTING
  virtual already_AddRefed<gfxImageSurface> Dump(Compositor* aCompositor)
  {
    MOZ_ASSERT(mInitParams.mStatus == InitParams::INITIALIZED);
    CompositorOGL* compositorOGL = static_cast<CompositorOGL*>(aCompositor);
    return mGL->GetTexImage(mTextureHandle, true, compositorOGL->GetFBOLayerProgramType());
  }
#endif

private:
  /**
   * Actually do the initialisation. Note that we leave our FBO bound, and so
   * calling this method is only suitable when about to use this render target.
   */
  void InitializeImpl()
  {
    MOZ_ASSERT(mInitParams.mStatus == InitParams::READY);

    mGL->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, mFBO);
    mGL->fFramebufferTexture2D(LOCAL_GL_FRAMEBUFFER,
                               LOCAL_GL_COLOR_ATTACHMENT0,
                               mInitParams.mFBOTextureTarget,
                               mTextureHandle,
                               0);

    // Making this call to fCheckFramebufferStatus prevents a crash on
    // PowerVR. See bug 695246.
    GLenum result = mGL->fCheckFramebufferStatus(LOCAL_GL_FRAMEBUFFER);
    if (result != LOCAL_GL_FRAMEBUFFER_COMPLETE) {
      nsAutoCString msg;
      msg.Append("Framebuffer not complete -- error 0x");
      msg.AppendInt(result, 16);
      msg.Append(", aFBOTextureTarget 0x");
      msg.AppendInt(mInitParams.mFBOTextureTarget, 16);
      msg.Append(", aRect.width ");
      msg.AppendInt(mInitParams.mRect.width);
      msg.Append(", aRect.height ");
      msg.AppendInt(mInitParams.mRect.height);
      NS_RUNTIMEABORT(msg.get());
    }

    mCompositor->PrepareViewport(mInitParams.mRect.width,
                                 mInitParams.mRect.height,
                                 gfxMatrix());
    mGL->fScissor(0, 0, mInitParams.mRect.width, mInitParams.mRect.height);
    if (mInitParams.mInit == INIT_MODE_CLEAR) {
      mGL->fClearColor(0.0, 0.0, 0.0, 0.0);
      mGL->fClear(LOCAL_GL_COLOR_BUFFER_BIT);
    }

    mInitParams.mStatus = InitParams::INITIALIZED;
  }

  InitParams mInitParams;
  CompositorOGL* mCompositor;
  GLContext* mGL;
  GLuint mTextureHandle;
  GLuint mFBO;
};

}
}

#endif /* MOZILLA_GFX_SURFACEOGL_H */
