#include "Icons.h"

#include <QByteArray>
#include <QPainter>
#include <QSvgRenderer>

namespace icons {
namespace {

/**
 * 每个图标的**内层** markup，原样取自 UI_SPEC §05。
 * 外层 <svg> 的属性统一在 `document()` 里补，与稿子一致：
 *   viewBox="0 0 24 24" fill="none" stroke="currentColor"
 *   stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"
 * 少数图标自带 fill="currentColor"（phone 一族、more、net-bars），
 * 那是它们本来就是实心的，不是描边的。
 */
const char* markupOf(Name name) {
  switch (name) {
    case Name::Mic:
      return R"(<rect x="9" y="2" width="6" height="12" rx="3"/>)"
             R"(<path d="M5 11a7 7 0 0 0 14 0"/><path d="M12 18v3"/><path d="M8.5 21h7"/>)";
    case Name::MicSlash:
      return R"(<rect x="9" y="2" width="6" height="12" rx="3"/>)"
             R"(<path d="M5 11a7 7 0 0 0 14 0"/><path d="M12 18v3"/><path d="M8.5 21h7"/>)"
             R"(<path d="M3 3 21 21"/>)";
    case Name::Video:
      return R"(<rect x="2" y="6" width="13" height="12" rx="2.5"/>)"
             R"(<path d="M15 10.5 22 7v10l-7-3.5z"/>)";
    case Name::VideoSlash:
      return R"(<rect x="2" y="6" width="13" height="12" rx="2.5"/>)"
             R"(<path d="M15 10.5 22 7v10l-7-3.5z"/><path d="M3 3 21 21"/>)";
    case Name::Phone:
      return R"(<path fill="currentColor" d="M7.2 3H4.3C3.6 3 3 3.6 3 4.3 3 13.5 10.5 21 19.7 21)"
             R"(c.7 0 1.3-.6 1.3-1.3v-2.9c0-.6-.4-1.1-1-1.2l-3-.6c-.5-.1-1 .1-1.3.5l-1.1 1.4)"
             R"(c-2.4-1.2-4.3-3.1-5.5-5.5l1.4-1.1c.4-.3.6-.8.5-1.3l-.6-3c-.1-.6-.6-1-1.2-1z"/>)";
    case Name::PhoneDown:
      // 这一段用 SVG( ) 作定界符：内容里有 `rotate(135 12 12)"`，
      // 默认的 R"( )" 会被那个 `)"` 提前截断。
      return R"SVG(<g transform="rotate(135 12 12)"><path fill="currentColor")SVG"
             R"SVG( d="M7.2 3H4.3C3.6 3 3 3.6 3 4.3 3 13.5 10.5 21 19.7 21c.7 0 1.3-.6 1.3-1.3)SVG"
             R"SVG(v-2.9c0-.6-.4-1.1-1-1.2l-3-.6c-.5-.1-1 .1-1.3.5l-1.1 1.4c-2.4-1.2-4.3-3.1)SVG"
             R"SVG(-5.5-5.5l1.4-1.1c.4-.3.6-.8.5-1.3l-.6-3c-.1-.6-.6-1-1.2-1z"/></g>)SVG";
    case Name::Xmark:
      return R"(<path d="M6 6l12 12M18 6 6 18"/>)";
    case Name::Minimize:
      return R"(<path d="M10 4H4v6"/><path d="M4 4l6 6"/>)"
             R"(<path d="M14 20h6v-6"/><path d="M20 20l-6-6"/>)";
    case Name::Expand:
      return R"(<path d="M4 10V4h6"/><path d="M10 10 4 4"/>)"
             R"(<path d="M20 14v6h-6"/><path d="M14 14l6 6"/>)";
    case Name::Speaker:
      return R"(<path d="M4 9v6h4l5 4V5L8 9H4z"/><path d="M17 8.5a5 5 0 0 1 0 7"/>)"
             R"(<path d="M19.5 6a8.5 8.5 0 0 1 0 12"/>)";
    case Name::SpeakerSlash:
      return R"(<path d="M4 9v6h4l5 4V5L8 9H4z"/><path d="M17 9l5 6M22 9l-5 6"/>)";
    case Name::CameraFlip:
      return R"(<path d="M3 8h3l1.5-2h9L18 8h3v11H3z"/><circle cx="12" cy="13" r="3.2"/>)"
             R"(<path d="M9.6 11.2 12 13"/><path d="M20 4.5a8 8 0 0 0-5-1.5"/>)";
    case Name::PersonAdd:
      return R"(<circle cx="9" cy="8" r="3.6"/><path d="M2.6 20a6.6 6.6 0 0 1 12.8 0"/>)"
             R"(<path d="M19 8v7M15.5 11.5h7"/>)";
    case Name::ChevronDown:
      return R"(<path d="M6 9l6 6 6-6"/>)";
    case Name::More:
      return R"(<circle cx="5.5" cy="12" r="1.6" fill="currentColor"/>)"
             R"(<circle cx="12" cy="12" r="1.6" fill="currentColor"/>)"
             R"(<circle cx="18.5" cy="12" r="1.6" fill="currentColor"/>)";
    case Name::ScreenShare:
      return R"(<rect x="3" y="4" width="18" height="13" rx="2"/><path d="M8 21h8"/>)"
             R"(<path d="M12 17v4"/><path d="m9 12 3-3 3 3"/><path d="M12 9v5"/>)";
    case Name::NetBars:
      return R"(<rect x="3" y="14" width="3.6" height="6" rx="1" fill="currentColor"/>)"
             R"(<rect x="10.2" y="9" width="3.6" height="11" rx="1" fill="currentColor"/>)"
             R"(<rect x="17.4" y="4" width="3.6" height="16" rx="1" fill="currentColor" opacity=".35"/>)";
    case Name::Grid:
      return R"(<rect x="3" y="4" width="7.5" height="7.5" rx="1.6"/>)"
             R"(<rect x="13.5" y="4" width="7.5" height="7.5" rx="1.6"/>)"
             R"(<rect x="3" y="14.5" width="7.5" height="5.5" rx="1.6"/>)"
             R"(<rect x="13.5" y="14.5" width="7.5" height="5.5" rx="1.6"/>)";
  }
  return "";
}

QByteArray document(Name name, const QColor& color) {
  // QSvgRenderer 不认 currentColor，所以在这里替换成实际色值——
  // 与 Web 端「同一份路径 + 外部着色」是同一个套路。
  QByteArray svg =
      R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" )"
      R"(stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">)";
  svg += markupOf(name);
  svg += "</svg>";
  return svg.replace("currentColor", color.name(QColor::HexRgb).toUtf8());
}

}  // namespace

QPixmap pixmap(Name name, int size, const QColor& color, qreal dpr) {
  const int physical = qMax(1, qRound(size * dpr));
  QPixmap canvas(physical, physical);
  canvas.setDevicePixelRatio(dpr);
  canvas.fill(Qt::transparent);

  QSvgRenderer renderer(document(name, color));
  QPainter painter(&canvas);
  painter.setRenderHint(QPainter::Antialiasing, true);
  // **目标矩形用逻辑像素，不是物理像素。**pixmap 上设了 devicePixelRatio，
  // QPainter 的坐标系已经是逻辑单位了；再传物理尺寸等于放大一倍，图会被裁掉。
  renderer.render(&painter, QRectF(0, 0, size, size));
  return canvas;
}

QIcon icon(Name name, int size, const QColor& color) {
  // 1x 与 2x 各出一张，交给 QIcon 按屏幕挑。
  QIcon result;
  result.addPixmap(pixmap(name, size, color, 1.0));
  result.addPixmap(pixmap(name, size, color, 2.0));
  return result;
}

}  // namespace icons
