#pragma once
#include "../State.hpp"
namespace cupuacu::actions
{
    inline DocumentTab *findOperationTab(State *state, uint64_t id)
    {
        if (state)
        {
            for (auto &tab : state->tabs)
            {
                if (tab.id == id)
                {
                    return &tab;
                }
            }
        }
        return nullptr;
    }
    inline bool operationCanceled(State *state, uint64_t tabId,
                                  DocumentOperation::Kind kind, uint64_t jobId)
    {
        const auto *tab = findOperationTab(state, tabId);
        return !tab || !tab->operation || tab->operation->kind != kind ||
               tab->operation->id != jobId || tab->operation->cancelRequested;
    }
    inline void finishOperation(State *state, uint64_t tabId,
                                DocumentOperation::Kind kind, uint64_t jobId)
    {
        if (auto *tab = findOperationTab(state, tabId);
            tab && tab->operation && tab->operation->kind == kind &&
            tab->operation->id == jobId)
        {
            tab->operation.reset();
        }
    }
    inline void updateOperation(State *state, uint64_t tabId,
                                DocumentOperation::Kind kind, uint64_t jobId,
                                const std::string &detail,
                                std::optional<double> progress)
    {
        if (auto *tab = findOperationTab(state, tabId);
            tab && tab->operation && tab->operation->kind == kind &&
            tab->operation->id == jobId)
        {
            tab->operation->detail = detail;
            tab->operation->progress = progress;
        }
    }
} // namespace cupuacu::actions
