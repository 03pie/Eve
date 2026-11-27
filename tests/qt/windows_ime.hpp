#pragma once

#include <QApplication>
#include <QInputMethodEvent>
#include <QLineEdit>
#include <QTest>
#include <iostream>
#include <string>
#include <windows.h>
#include <imm.h>

namespace eve::test {
/** Exercised explicitly on a real Windows desktop with a Chinese IME installed.
 * Save and restore only the test window's input context. */
inline void verifyNativePinyin(QLineEdit* edit)
{
    CHECK(QApplication::platformName() == "windows");
    auto window = edit->window();
    window->raise();
    window->activateWindow();
    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    if (GetForegroundWindow() != hwnd) {
        const DWORD current = GetCurrentThreadId();
        const DWORD foreground = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
        const bool attached = foreground && foreground != current && AttachThreadInput(current, foreground, TRUE);
        SetForegroundWindow(hwnd);
        if (attached) AttachThreadInput(current, foreground, FALSE);
    }
    CHECK(QTest::qWaitForWindowActive(window));
    edit->setFocus(Qt::ShortcutFocusReason);
    QTest::qWait(100);
    CHECK(edit->hasFocus() && edit->text().isEmpty());
    struct Composition final : QObject {
        QString preedit;
        QString committed;
        int submissions{};
        bool eventFilter(QObject*, QEvent* event) override
        {
            if (event->type() == QEvent::InputMethod) {
                const auto input = static_cast<QInputMethodEvent*>(event);
                preedit = input->preeditString();
                committed += input->commitString();
            }
            return false;
        }
    } observed;
    edit->installEventFilter(&observed);
    QObject::connect(edit, &QLineEdit::returnPressed, &observed, [&] { ++observed.submissions; });
    const auto layout = GetKeyboardLayout(0);
    CHECK(PRIMARYLANGID(LOWORD(reinterpret_cast<ULONG_PTR>(layout))) == LANG_CHINESE);
    struct InputContext {
        HWND window;
        HIMC handle;
        BOOL open;
        DWORD conversion{}, sentence{};
        bool hasMode{};
        ~InputContext()
        {
            if (handle) {
                if (hasMode) ImmSetConversionStatus(handle, conversion, sentence);
                ImmSetOpenStatus(handle, open);
                ImmReleaseContext(window, handle);
            }
        }
    } context{hwnd, ImmGetContext(hwnd), FALSE};
    CHECK(context.handle);
    context.open = ImmGetOpenStatus(context.handle);
    context.hasMode = ImmGetConversionStatus(context.handle, &context.conversion, &context.sentence);
    CHECK(context.hasMode);
    ImmSetOpenStatus(context.handle, TRUE);
    ImmSetConversionStatus(context.handle, context.conversion | IME_CMODE_NATIVE, context.sentence);
    const auto key = [hwnd, layout](WORD code) {
        CHECK(GetForegroundWindow() == hwnd);
        INPUT pair[2]{};
        for (auto& input : pair) {
            input.type = INPUT_KEYBOARD;
            input.ki.wScan = static_cast<WORD>(MapVirtualKeyExW(code, MAPVK_VK_TO_VSC, layout));
            input.ki.dwFlags = KEYEVENTF_SCANCODE;
        }
        pair[1].ki.dwFlags |= KEYEVENTF_KEYUP;
        CHECK(SendInput(2, pair, sizeof(INPUT)) == 2);
        QTest::qWait(100);
    };
    key('M');
    key('I');
    CHECK(!observed.preedit.isEmpty() && edit->text().isEmpty());
    std::cout << "preedit=" << observed.preedit.toUtf8().constData() << std::endl;
    key(VK_ESCAPE);
    CHECK(observed.preedit.isEmpty() && observed.committed.isEmpty() && edit->hasFocus());
    key('M');
    key('I');
    key(VK_RETURN);
    CHECK(!observed.committed.isEmpty() && observed.submissions == 0 && edit->hasFocus());
    std::cout << "compositionEnter=" << observed.committed.toUtf8().constData() << " returnPressed=0" << std::endl;
    observed.committed.clear();
    edit->clear();
    for (char letter : std::string("MIANBAN")) key(letter);
    key(VK_SPACE);
    CHECK(observed.committed == QStringLiteral("面板") && edit->text() == observed.committed);
    CHECK(observed.submissions == 0);
    std::cout << "candidate=" << observed.committed.toUtf8().constData() << " returnPressed=0" << std::endl;
}
}
