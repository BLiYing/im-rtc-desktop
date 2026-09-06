/**
 * NativeSurfacePattern_mac.mm —— 假渲染器的 macOS 实现。
 *
 * **刻意不走 Qt 的绘制**：直接往 `NSView` 的 layer 上加一层 `CALayer`，
 * 这正是引擎将来拿到 `NSView*` 之后会做的事（libwebrtc 的
 * `RTCMTLNSVideoView` 也是 layer-backed）。用它来复现真实条件下的
 * 层级 / 缩放 / DPI 行为，而不是用 Qt 画一个"看起来像"的方块自欺欺人。
 *
 * 图案本身有意做得能一眼看出问题：
 *   - 满幅渐变 → 层有没有铺满、有没有错位
 *   - 四角直角标记 → 有没有被裁掉
 *   - 中间一行文字写着**像素尺寸与 backing scale** → DPI 对不对
 */

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <QString>

#include "NativeSurface.h"

namespace {

/** 我们加的那层的名字，方便找回来与删掉——别对着 sublayers 数数组下标。 */
NSString* const kPatternLayerName = @"imrtc.testPattern";

CALayer* findPatternLayer(NSView* view) {
  for (CALayer* layer in view.layer.sublayers) {
    if ([layer.name isEqualToString:kPatternLayerName]) return layer;
  }
  return nil;
}

}  // namespace

bool NativeSurface::testPatternSupported() { return true; }

void NativeSurface::applyTestPattern() {
  NSView* view = reinterpret_cast<NSView*>(nativeHandle());
  if (view == nil) return;

  // wantsLayer 必须开，否则 view.layer 是 nil。Qt 的原生 widget 默认已经是
  // layer-backed，但**不能假设**——宿主的 Qt 版本与后端都可能不同。
  view.wantsLayer = YES;
  if (view.layer == nil) return;

  CALayer* pattern = findPatternLayer(view);
  if (pattern == nil) {
    pattern = [CALayer layer];
    pattern.name = kPatternLayerName;
    pattern.masksToBounds = YES;
    [view.layer addSublayer:pattern];
  }

  const CGFloat scale = view.window != nil ? view.window.backingScaleFactor : 1.0;
  pattern.contentsScale = scale;
  // **只用一种机制。**试过再加 autoresizingMask，结果它和下面显式设的 frame
  // 叠加相乘：控件 153×88，层却变成 190×90。两种伸缩机制不能同时开。
  // 这里选显式同步，因为形状层的路径与文字层的位置本来就要重算。
  pattern.autoresizingMask = kCALayerNotSizable;

  // 渐变底，看得出有没有铺满、有没有错位。
  CAGradientLayer* gradient = nil;
  for (CALayer* sub in pattern.sublayers) {
    if ([sub isKindOfClass:[CAGradientLayer class]]) gradient = (CAGradientLayer*)sub;
  }
  if (gradient == nil) {
    gradient = [CAGradientLayer layer];
    gradient.startPoint = CGPointMake(0, 0);
    gradient.endPoint = CGPointMake(1, 1);
    [pattern addSublayer:gradient];
  }
  gradient.colors = @[
    (id)[NSColor colorWithSRGBRed:0.16 green:0.20 blue:0.31 alpha:1.0].CGColor,
    (id)[NSColor colorWithSRGBRed:0.05 green:0.07 blue:0.09 alpha:1.0].CGColor,
  ];

  // 四角直角标记：被裁掉的话一眼就能看出来。
  CAShapeLayer* corners = nil;
  for (CALayer* sub in pattern.sublayers) {
    if ([sub isKindOfClass:[CAShapeLayer class]]) corners = (CAShapeLayer*)sub;
  }
  if (corners == nil) {
    corners = [CAShapeLayer layer];
    corners.fillColor = nil;
    corners.lineWidth = 2.0;
    corners.strokeColor = [NSColor colorWithSRGBRed:0.24 green:0.86 blue:0.52 alpha:1.0].CGColor;
    [pattern addSublayer:corners];
  }

  // 文字层写清楚它自己有多大、scale 是多少——DPI 错了这里立刻露馅。
  CATextLayer* text = nil;
  for (CALayer* sub in pattern.sublayers) {
    if ([sub isKindOfClass:[CATextLayer class]]) text = (CATextLayer*)sub;
  }
  if (text == nil) {
    text = [CATextLayer layer];
    text.alignmentMode = kCAAlignmentCenter;
    text.font = (__bridge CFTypeRef) @"Menlo";
    text.fontSize = 11.0;
    text.foregroundColor = [NSColor whiteColor].CGColor;
    [pattern addSublayer:text];
  }
  text.contentsScale = scale;

  syncPatternGeometry();
}

void NativeSurface::removeTestPattern() {
  NSView* view = reinterpret_cast<NSView*>(winId());
  if (view == nil || view.layer == nil) return;
  CALayer* pattern = findPatternLayer(view);
  if (pattern != nil) [pattern removeFromSuperlayer];
}

QRectF NativeSurface::patternGeometry() const {
  NSView* view = reinterpret_cast<NSView*>(const_cast<NativeSurface*>(this)->winId());
  if (view == nil || view.layer == nil) return {};
  CALayer* pattern = findPatternLayer(view);
  if (pattern == nil) return {};
  const CGRect frame = pattern.frame;
  return QRectF(frame.origin.x, frame.origin.y, frame.size.width, frame.size.height);
}

qreal NativeSurface::patternScale() const {
  NSView* view = reinterpret_cast<NSView*>(const_cast<NativeSurface*>(this)->winId());
  if (view == nil || view.layer == nil) return 0.0;
  CALayer* pattern = findPatternLayer(view);
  return pattern == nil ? 0.0 : pattern.contentsScale;
}

void NativeSurface::syncPatternGeometry() {
  NSView* view = reinterpret_cast<NSView*>(nativeHandle());
  if (view == nil || view.layer == nil) return;
  CALayer* pattern = findPatternLayer(view);
  if (pattern == nil) return;

  // **不要在这里做隐式动画。**Qt 拉窗口时每一帧都会走到这里，
  // CALayer 默认的 0.25s 隐式动画会让原生层永远追不上 Qt 的布局——
  // 表现就是"拖动窗口时画面慢半拍、边缘露出底色"。
  [CATransaction begin];
  [CATransaction setDisableActions:YES];

  /*
    **几何以 Qt 控件的尺寸为准，不读 view.layer.bounds。**

    踩过的坑：`QWidget::resizeEvent` 触发时，Qt 还没把新几何推给底层的 NSView，
    这时读 `layer.bounds` 拿到的是**上一次**的尺寸。表现是格子拉大之后原生层
    卡在初始大小，右边露出一条底色——而且之后再也不会自己纠正，因为不会再有
    resizeEvent 了。实测：格子 157×92、Qt 控件 153×88，而 layer 停在 116×86。

    真正的渲染器（WebRTCAdapter）在 NSView 那一侧看不到 Qt 的事件，
    应当靠 `autoresizingMask` 或 `NSViewFrameDidChangeNotification`，
    **不要**在收到宿主的某个回调时去读一次 bounds 就完事。
  */
  const CGRect box = CGRectMake(0, 0, width(), height());
  pattern.frame = box;

  for (CALayer* sub in pattern.sublayers) {
    if ([sub isKindOfClass:[CAGradientLayer class]]) {
      sub.frame = box;
    } else if ([sub isKindOfClass:[CAShapeLayer class]]) {
      CGMutablePathRef path = CGPathCreateMutable();
      const CGFloat arm = 14.0;
      const CGFloat pad = 5.0;
      const CGFloat maxX = CGRectGetMaxX(box) - pad;
      const CGFloat maxY = CGRectGetMaxY(box) - pad;
      // 左下
      CGPathMoveToPoint(path, nullptr, pad, pad + arm);
      CGPathAddLineToPoint(path, nullptr, pad, pad);
      CGPathAddLineToPoint(path, nullptr, pad + arm, pad);
      // 右下
      CGPathMoveToPoint(path, nullptr, maxX - arm, pad);
      CGPathAddLineToPoint(path, nullptr, maxX, pad);
      CGPathAddLineToPoint(path, nullptr, maxX, pad + arm);
      // 右上
      CGPathMoveToPoint(path, nullptr, maxX, maxY - arm);
      CGPathAddLineToPoint(path, nullptr, maxX, maxY);
      CGPathAddLineToPoint(path, nullptr, maxX - arm, maxY);
      // 左上
      CGPathMoveToPoint(path, nullptr, pad + arm, maxY);
      CGPathAddLineToPoint(path, nullptr, pad, maxY);
      CGPathAddLineToPoint(path, nullptr, pad, maxY - arm);
      ((CAShapeLayer*)sub).path = path;
      CGPathRelease(path);
      sub.frame = box;
    } else if ([sub isKindOfClass:[CATextLayer class]]) {
      const CGFloat scale = view.window != nil ? view.window.backingScaleFactor : 1.0;
      // 两行短的，别拼成一长条——格子只有百来点宽，长了会被两头切掉。
      NSString* caption =
          [NSString stringWithFormat:@"%@\n%.0f×%.0f @%.0fx", label_.toNSString(),
                                     box.size.width, box.size.height, scale];
      ((CATextLayer*)sub).string = caption;
      ((CATextLayer*)sub).contentsScale = scale;
      ((CATextLayer*)sub).wrapped = YES;
      sub.frame = CGRectMake(0, CGRectGetMidY(box) - 16, box.size.width, 32);
    }
  }

  [CATransaction commit];
}
