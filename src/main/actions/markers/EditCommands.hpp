#pragma once

#include "../Undoable.hpp"
#include "../../Document.hpp"
#include "../../State.hpp"
#include "../../file/OverwritePreservationMutation.hpp"
#include "../../gui/MainViewAccess.hpp"
#include "../../gui/Waveform.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cupuacu::actions::markers
{
    struct MarkerSnapshot
    {
        bool exists = false;
        DocumentMarker marker{};
        std::optional<uint64_t> selectedMarkerId;
    };

    inline std::optional<DocumentMarker> findMarkerById(const Document &document,
                                                        const uint64_t id)
    {
        const auto &markers = document.getMarkers();
        const auto it = std::find_if(
            markers.begin(), markers.end(),
            [&](const DocumentMarker &marker) { return marker.id == id; });
        if (it == markers.end())
        {
            return std::nullopt;
        }

        return *it;
    }

    inline uint64_t nextMarkerIdForInsert(const Document &document)
    {
        uint64_t maxId = 0;
        for (const auto &marker : document.getMarkers())
        {
            maxId = std::max(maxId, marker.id);
        }
        return maxId + 1;
    }

    inline MarkerSnapshot missingMarkerSnapshot(
        const uint64_t id, const std::optional<uint64_t> selectedMarkerId)
    {
        return {.exists = false,
                .marker = DocumentMarker{.id = id},
                .selectedMarkerId = selectedMarkerId};
    }

    inline std::optional<MarkerSnapshot> currentMarkerSnapshot(
        const State *state, const uint64_t id)
    {
        if (!state)
        {
            return std::nullopt;
        }

        const auto marker =
            findMarkerById(state->getActiveDocumentSession().document, id);
        if (!marker.has_value())
        {
            return std::nullopt;
        }

        return MarkerSnapshot{
            .exists = true,
            .marker = *marker,
            .selectedMarkerId = state->getActiveViewState().selectedMarkerId,
        };
    }

    inline void refreshMarkerUi(State *state)
    {
        if (!state)
        {
            return;
        }

        gui::requestMainViewRefresh(state);
        gui::Waveform::setAllWaveformsDirty(state);
    }

    class SetMarkerState final : public Undoable
    {
    public:
        explicit SetMarkerState(State *stateToUse, MarkerSnapshot oldStateToUse,
                                MarkerSnapshot newStateToUse,
                                std::string descriptionToUse)
            : Undoable(stateToUse), oldState(std::move(oldStateToUse)),
              newState(std::move(newStateToUse)),
              description(std::move(descriptionToUse))
        {
            updateGui = [state = stateToUse]() { refreshMarkerUi(state); };
        }

        void setNewState(MarkerSnapshot newStateToUse)
        {
            newState = std::move(newStateToUse);
        }

        const MarkerSnapshot &getOldState() const
        {
            return oldState;
        }

        const MarkerSnapshot &getNewState() const
        {
            return newState;
        }

        void redo() override
        {
            apply(newState);
        }

        void undo() override
        {
            apply(oldState);
        }

        std::string getUndoDescription() override
        {
            return description;
        }

        std::string getRedoDescription() override
        {
            return description;
        }

        [[nodiscard]] bool canPersistForRestart() const override
        {
            return false;
        }

        [[nodiscard]] cupuacu::file::OverwritePreservationMutation
        overwritePreservationMutation() const override
        {
            return cupuacu::file::OverwritePreservationMutationHelper::compatible();
        }

    private:
        MarkerSnapshot oldState;
        MarkerSnapshot newState;
        std::string description;

        void apply(const MarkerSnapshot &snapshot)
        {
            auto &document = state->getActiveDocumentSession().document;
            auto markers = document.getMarkers();
            const auto existing = std::find_if(
                markers.begin(), markers.end(),
                [&](const DocumentMarker &marker)
                { return marker.id == snapshot.marker.id; });

            if (existing != markers.end())
            {
                if (snapshot.exists)
                {
                    *existing = snapshot.marker;
                }
                else
                {
                    markers.erase(existing);
                }
            }
            else if (snapshot.exists)
            {
                markers.push_back(snapshot.marker);
            }

            document.replaceMarkers(std::move(markers));
            state->getActiveViewState().selectedMarkerId =
                snapshot.selectedMarkerId;
        }
    };

    inline uint64_t insertMarkerAtCursor(State *state)
    {
        if (!state)
        {
            return 0;
        }

        auto &session = state->getActiveDocumentSession();
        if (session.document.getChannelCount() <= 0 ||
            session.document.getSampleRate() <= 0)
        {
            return 0;
        }

        const uint64_t markerId = nextMarkerIdForInsert(session.document);
        const auto oldSelection = state->getActiveViewState().selectedMarkerId;
        const auto oldState = missingMarkerSnapshot(markerId, oldSelection);
        const auto newState = MarkerSnapshot{
            .exists = true,
            .marker = DocumentMarker{
                .id = markerId,
                .frame = std::clamp(session.cursor, int64_t{0},
                                    session.document.getFrameCount()),
                .label = {},
            },
            .selectedMarkerId = markerId,
        };
        state->addAndDoUndoable(std::make_shared<SetMarkerState>(
            state, oldState, newState, "Insert marker"));
        return markerId;
    }

    inline void deleteMarker(
        State *state, const uint64_t markerId,
        const std::optional<uint64_t> nextSelectedMarkerId = std::nullopt)
    {
        if (!state)
        {
            return;
        }

        const auto oldState = currentMarkerSnapshot(state, markerId);
        if (!oldState.has_value())
        {
            return;
        }

        const auto newState =
            missingMarkerSnapshot(markerId, nextSelectedMarkerId);
        state->addAndDoUndoable(std::make_shared<SetMarkerState>(
            state, *oldState, newState, "Delete marker"));
    }
} // namespace cupuacu::actions::markers
