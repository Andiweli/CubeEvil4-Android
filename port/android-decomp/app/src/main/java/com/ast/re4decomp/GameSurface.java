package com.ast.re4decomp;

import android.content.Context;
import android.opengl.GLSurfaceView;

/**
 * The view the game's picture is drawn into: GLSurfaceView (OpenGL ES, as always) or VulkanSurface (Vulkan,
 * port/docs/vulkan-bauplan.md). CoreActivity calls these on either; the render mode is GLSurfaceView's
 * (RENDERMODE_CONTINUOUSLY: a picture after each one, RENDERMODE_WHEN_DIRTY: one per requestRender).
 */
interface GameSurface {
  void requestRender();
  void setRenderMode(int mode);
  void onPause();
  void onResume();

  /** OpenGL ES: GLSurfaceView itself. */
  final class Gl extends GLSurfaceView implements GameSurface {
    Gl(Context context) {
      super(context);
    }
  }
}
