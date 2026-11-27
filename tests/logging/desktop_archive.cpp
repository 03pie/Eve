/** A single run remains queryable across desktop attachment and destruction. */
#include "runtime_log_archive_p.hpp"
#include <eve/ui/ela_shell.hpp>
#include <QApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
#include <set>

#define REQUIRE(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (false)

int main(int argc, char** argv)
{
    using namespace eve;
    using namespace eve::ui;
    const QApplication app(argc, argv);
    const QTemporaryDir workspace;
    try {
        REQUIRE(workspace.isValid());
        const auto scheduler = std::make_shared<qt::QtScheduler>();
        const auto archive = std::make_shared<ui::detail::RuntimeLogArchive>(workspace.path(), "desktop-test");
        const auto before = archive->writePlatform("before activation").value();
        std::uint64_t during;
        {
            ElaShell shell(scheduler);
            ui::detail::attachDesktopArchive(shell, archive).value();
            REQUIRE(!ui::detail::attachDesktopArchive(shell, archive).hasValue());
            during = archive->writePlatform("desktop active").value();
            const auto rows = shell.output()->snapshot().value();
            REQUIRE(rows.size() == 3 && rows.front().sequence == before && rows.back().sequence == during);
            REQUIRE(rows.front().text == "before activation" && rows.back().text == "desktop active");
            auto closing = shell.close();
            QElapsedTimer elapsed;
            elapsed.start();
            while (!closing.isReady() && elapsed.elapsed() < 8000)
                QTest::qWait(5);
            REQUIRE(closing.isReady() && closing.tryResult()->hasValue());
        }
        const auto after = archive->writePlatform("desktop stopped", OutputLevel::Info, {{}, "CLEANUP"}).value();
        REQUIRE(before < during && during < after);
        archive->finish(true);
        const auto metadata = ui::detail::readSavedLogRun(workspace.path(), archive->currentRun());
        REQUIRE(metadata && metadata->ended);
        std::set<std::uint64_t> sequences;
        REQUIRE(ui::detail::scanSavedDescending(workspace.path(), archive->currentRun(), 0, [&](const auto& row) {
            REQUIRE(sequences.insert(row.entry.sequence).second);
            return true;
        }).isEmpty());
        REQUIRE(sequences.size() == 4 && *sequences.begin() == before && *sequences.rbegin() == after);
        const auto final = archive->locate(archive->currentRun(), after);
        REQUIRE(final && final->diagnosticCode == "CLEANUP");
        scheduler->close().value();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
