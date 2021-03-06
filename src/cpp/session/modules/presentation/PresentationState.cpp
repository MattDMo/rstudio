/*
 * PresentationState.cpp
 *
 * Copyright (C) 2009-12 by RStudio, Inc.
 *
 * Unless you have received this program directly from RStudio pursuant
 * to the terms of a commercial license agreement with RStudio, then
 * this program is licensed to you under the terms of version 3 of the
 * GNU Affero General Public License. This program is distributed WITHOUT
 * ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING THOSE OF NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Please refer to the
 * AGPL (http://www.gnu.org/licenses/agpl-3.0.txt) for more details.
 *
 */


#include "PresentationState.hpp"

#include <core/FilePath.hpp>
#include <core/Settings.hpp>

#include <session/SessionModuleContext.hpp>

using namespace core;

namespace session {
namespace modules { 
namespace presentation {
namespace state {

namespace {
      
struct PresentationState
{
   PresentationState()
      : active(false), authorMode(false), usingRmd(false), slideIndex(0)
   {
   }

   bool active;
   std::string paneCaption;
   bool authorMode;
   bool usingRmd;
   FilePath directory;
   int slideIndex;
};

// write-through cache of presentation state
PresentationState s_presentationState;

FilePath presentationStatePath()
{
   FilePath path = module_context::scopedScratchPath().childPath("presentation");
   Error error = path.ensureDirectory();
   if (error)
      LOG_ERROR(error);
   return path.childPath("presentation-state");
}

void savePresentationState(const PresentationState& state)
{
   // update write-through cache
   s_presentationState = state;

   // save to disk
   Settings settings;
   Error error = settings.initialize(presentationStatePath());
   if (error)
   {
      LOG_ERROR(error);
      return;
   }
   settings.beginUpdate();
   settings.set("active", state.active);
   settings.set("author-mode", state.authorMode);
   settings.set("using-rmd", state.usingRmd);
   settings.set("pane-caption", state.paneCaption);
   settings.set("directory",
                module_context::createAliasedPath(state.directory));
   settings.set("slide-index", state.slideIndex);
   settings.endUpdate();
}

void loadPresentationState()
{
   FilePath statePath = presentationStatePath();
   if (statePath.exists())
   {
      Settings settings;
      Error error = settings.initialize(presentationStatePath());
      if (error)
         LOG_ERROR(error);

      s_presentationState.active = settings.getBool("active", false);
      s_presentationState.authorMode = settings.getBool("author-mode", false);
      s_presentationState.usingRmd = settings.getBool("using-rmd", false);
      s_presentationState.paneCaption = settings.get("pane-caption", "Presentaiton");
      s_presentationState.directory = module_context::resolveAliasedPath(
                                                   settings.get("directory"));
      s_presentationState.slideIndex = settings.getInt("slide-index", 0);
   }
   else
   {
      s_presentationState = PresentationState();
   }
}

} // anonymous namespace


void init(const FilePath& directory,
          const std::string& paneCaption,
          bool authorMode)
{
   PresentationState state;
   state.active = true;
   state.paneCaption = paneCaption;
   state.authorMode = authorMode;
   state.usingRmd = directory.childPath("slides.Rmd").exists();
   state.directory = directory;
   state.slideIndex = 0;
   savePresentationState(state);
}

void setSlideIndex(int index)
{
   s_presentationState.slideIndex = index;
   savePresentationState(s_presentationState);
}

bool isActive()
{
   return s_presentationState.active;
}

bool authorMode()
{
   return s_presentationState.authorMode;
}

FilePath directory()
{
   return s_presentationState.directory;
}

void clear()
{
   savePresentationState(PresentationState());
}

json::Value asJson()
{
   json::Object stateJson;
   stateJson["active"] = s_presentationState.active;
   stateJson["author_mode"] = s_presentationState.authorMode;
   stateJson["using_rmd"] = s_presentationState.usingRmd;
   stateJson["pane_caption"] = s_presentationState.paneCaption;
   stateJson["directory"] = module_context::createAliasedPath(
                                                s_presentationState.directory);
   stateJson["slide_index"] = s_presentationState.slideIndex;
   return stateJson;
}

Error initialize()
{
   loadPresentationState();
   return Success();
}

} // namespace state
} // namespace presentation
} // namespace modules
} // namesapce session

