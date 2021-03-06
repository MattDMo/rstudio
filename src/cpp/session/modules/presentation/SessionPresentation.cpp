/*
 * SessionPresentation.cpp
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


#include "SessionPresentation.hpp"

#include <iostream>

#include <boost/utility.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <core/Exec.hpp>
#include <core/FileSerializer.hpp>
#include <core/HtmlUtils.hpp>
#include <core/markdown/Markdown.hpp>
#include <core/text/TemplateFilter.hpp>
#include <core/system/Process.hpp>

#include <r/RExec.hpp>
#include <r/RSexp.hpp>
#include <r/RRoutines.hpp>

#include <session/SessionModuleContext.hpp>
#include <session/projects/SessionProjects.hpp>

#include "PresentationState.hpp"

#include "SlideParser.hpp"
#include "SlideRenderer.hpp"

using namespace core;

namespace session {
namespace modules { 
namespace presentation {

namespace {

FilePath presentationResourcesPath()
{
   return session::options().rResourcesPath().complete("presentation");
}

SEXP rs_showPresentation(SEXP directorySEXP,
                         SEXP tabCaptionSEXP,
                         SEXP authorModeSEXP)
{
   try
   {
      if (session::options().programMode() == kSessionProgramModeServer)
      {
         // validate path
         FilePath dir(r::sexp::asString(directorySEXP));
         if (!dir.exists())
            throw r::exec::RErrorException("Directory " + dir.absolutePath() +
                                           " does not exist.");

         // initialize state
         presentation::state::init(dir,
                                   r::sexp::asString(tabCaptionSEXP),
                                   r::sexp::asLogical(authorModeSEXP));

         // notify the client
         ClientEvent event(client_events::kShowPresentationPane,
                           presentation::state::asJson());
         module_context::enqueClientEvent(event);
      }
      else
      {
         throw r::exec::RErrorException("Presentations are not supported "
                                        "in desktop mode.");
      }
   }
   catch(const r::exec::RErrorException& e)
   {
      r::exec::error(e.message());
   }

   return R_NilValue;
}

core::Error setPresentationSlideIndex(const json::JsonRpcRequest& request,
                                      json::JsonRpcResponse*)
{
   int index;
   Error error = json::readParam(request.params, 0, &index);
   if (error)
      return error;

   presentation::state::setSlideIndex(index);

   return Success();
}

core::Error closePresentationPane(const json::JsonRpcRequest&,
                                 json::JsonRpcResponse*)
{
   presentation::state::clear();

   return Success();
}

class ResourceFiles : boost::noncopyable
{
private:
   ResourceFiles() {}

public:
   std::string get(const std::string& path)
   {
      if (presentation::state::authorMode())
      {
         return module_context::resourceFileAsString(path);
      }
      else
      {
         if (cache_.find(path) == cache_.end())
            cache_[path] = module_context::resourceFileAsString(path);
         return cache_[path];
      }
   }

private:
   friend ResourceFiles& resourceFiles();
   std::map<std::string,std::string> cache_;
};

ResourceFiles& resourceFiles()
{
   static ResourceFiles instance;
   return instance;
}



std::string revealResource(const std::string& path,
                           bool embed,
                           const std::string& extraAttribs)
{
   // determine type
   bool isCss = boost::algorithm::ends_with(path, "css");

   // generate code for link vs. embed
   std::string code;
   if (embed)
   {
      if (isCss)
      {
         code = "<style type=\"text/css\" " + extraAttribs + " >\n" +
               resourceFiles().get("presentation/" + path) + "\n"
               + "</style>";
      }
      else
      {
         code = "<script type=\"text/javascript\" " + extraAttribs + " >\n" +
               resourceFiles().get("presentation/" + path) + "\n"
               + "</script>";
      }
   }
   else
   {
      if (isCss)
      {
         code = "<link rel=\"stylesheet\" href=\"" + path + "\" "
                 + extraAttribs + " >";
      }
      else
      {
         code = "<script src=\"" + path + "\" " + extraAttribs + " ></script>";
      }
   }

   return code;
}

std::string revealEmbed(const std::string& path,
                        const std::string& extraAttribs = std::string())
{
   return revealResource(path, true, extraAttribs);
}

std::string revealLink(const std::string& path,
                       const std::string& extraAttribs = std::string())
{
   return revealResource(path, false, extraAttribs);
}


std::string mathjaxIfRequired(const std::string& contents)
{
   if (markdown::isMathJaxRequired(contents))
      return resourceFiles().get("presentation/mathjax.html");
   else
      return std::string();
}

std::string mathjaxLocal(const std::string& mathjax)
{
   return boost::algorithm::replace_first_copy(
        mathjax,
        "https://c328740.ssl.cf1.rackcdn.com/mathjax/2.0-latest",
        "mathjax");
}

void setLocalWebFonts(std::map<std::string,std::string>* pVars)
{
   (*pVars)["news_cycle_webfont_url"] = "revealjs/fonts/NewsCycle.css";
   (*pVars)["lato_webfont_url"] = "revealjs/fonts/Lato.css";
}

void setRemoteWebFonts(std::map<std::string,std::string>* pVars)
{
   (*pVars)["news_cycle_webfont_url"] =
     "https://fonts.googleapis.com/css?family=News+Cycle:400,700";
   (*pVars)["lato_webfont_url"] =
     "https://fonts.googleapis.com/css?family=Lato:400,700,400italic,700italic";
}

bool hasKnitrVersion1()
{
   bool hasVersion = false;
   Error error = r::exec::RFunction(".rs.hasKnitrVersion1").call(&hasVersion);
   if (error)
      LOG_ERROR(error);
   return hasVersion;
}

bool knitSlides(const FilePath& slidesRmd, std::string* pErrMsg)
{
   // R binary
   FilePath rProgramPath;
   Error error = module_context::rScriptPath(&rProgramPath);
   if (error)
   {
      *pErrMsg = error.summary();
      return false;
   }

   // confirm correct version of knitr
   if (!hasKnitrVersion1())
   {
      *pErrMsg = "knitr version 1.0 or greater is required for presentations";
      return false;
   }

   // args
   std::vector<std::string> args;
   args.push_back("--silent");
   args.push_back("--no-save");
   args.push_back("--no-restore");
   args.push_back("-e");
   boost::format fmt("library(knitr); "
                     "opts_chunk$set(cache=TRUE,     "
                     "               results='hide', "
                     "               tidy=FALSE,     "
                     "               warning=FALSE,  "
                     "               message=FALSE,  "
                     "               comment=NA);    "
                     "knit('%2%', encoding='%1%');");
   std::string encoding = projects::projectContext().defaultEncoding();
   std::string cmd = boost::str(fmt % encoding % slidesRmd.filename());
   args.push_back(cmd);

   // options
   core::system::ProcessOptions options;
   core::system::ProcessResult result;
   options.workingDir = slidesRmd.parent();

   // run knit
   error = core::system::runProgram(
            core::string_utils::utf8ToSystem(rProgramPath.absolutePath()),
            args,
            "",
            options,
            &result);
   if (error)
   {
      *pErrMsg = error.summary();
      return false;
   }
   else if (result.exitStatus != EXIT_SUCCESS)
   {
      *pErrMsg = "Error occurred during knit: " + result.stdErr;
      return false;
   }
   else
   {
      return true;
   }
}

void handlePresentationPaneRequest(const http::Request& request,
                                   http::Response* pResponse)
{
   // return not found if presentation isn't active
   if (!presentation::state::isActive())
   {
      pResponse->setError(http::status::NotFound, request.uri() + " not found");
      return;
   }

   // get the requested path
   std::string path = http::util::pathAfterPrefix(request, "/presentation/");

   // special handling for the root (process template)
   if (path.empty())
   {
      // look for slides.Rmd and knit it if we are in author mode
      FilePath presDir = presentation::state::directory();
      if (presentation::state::authorMode())
      {
         FilePath rmdFile = presDir.complete("slides.Rmd");
         if (rmdFile.exists())
         {
            std::string errMsg;
            if (!knitSlides(rmdFile, &errMsg))
            {
               pResponse->setError(http::status::InternalServerError,
                                   errMsg);
               return;
            }
         }
      }

      // look for slides.md
      FilePath slidesFile = presDir.complete("slides.md");
      if (!slidesFile.exists())
      {
         pResponse->setError(http::status::NotFound,
                             "slides.md file not found in " +
                             presentation::state::directory().absolutePath());
         return;
      }

      // parse the slides
      presentation::SlideDeck slideDeck;
      Error error = slideDeck.readSlides(slidesFile);
      if (error)
      {
         LOG_ERROR(error);
         pResponse->setError(http::status::InternalServerError,
                             error.summary());
         return;
      }

      // render the slides
      std::string slides, revealConfig, initCommands, slideCommands;
      error = presentation::renderSlides(slideDeck,
                                         &slides,
                                         &revealConfig,
                                         &initCommands,
                                         &slideCommands);
      if (error)
      {
         LOG_ERROR(error);
         pResponse->setError(http::status::InternalServerError,
                             error.summary());
         return;
      }

      // get user css if it exists
      std::string userSlidesCss;
      FilePath cssPath = presentation::state::directory().complete("slides.css");
      if (cssPath.exists())
      {
         Error error = core::readStringFromFile(cssPath, &userSlidesCss);
         if (error)
            LOG_ERROR(error);
      }

      // build template variables
      std::map<std::string,std::string> vars;
      vars["title"] = slideDeck.title();
      vars["user_slides_css"] = userSlidesCss;
      vars["preamble"] = slideDeck.preamble();
      vars["slides"] = slides;
      vars["slides_css"] =  resourceFiles().get("presentation/slides.css");
      vars["r_highlight"] = resourceFiles().get("r_highlight.html");
      vars["reveal_config"] = revealConfig;

      try
      {
         // get base directory
         FilePath dirPath = presentation::state::directory();

         // get template
         std::string presentationTemplate =
                              resourceFiles().get("presentation/slides.html");

          // generate standalone version

         // embedded versions of reveal assets
         const char * const kMediaPrint = "media=\"print\"";
         vars["reveal_print_pdf_css"] = revealEmbed("revealjs/css/print/pdf.css",
                                                    kMediaPrint);
         vars["reveal_css"] = revealEmbed("revealjs/css/reveal.min.css");
         vars["reveal_theme_css"] = revealEmbed("revealjs/css/theme/simple.css");
         vars["reveal_head_js"] = revealEmbed("revealjs/lib/js/head.min.js");
         vars["reveal_js"] = revealEmbed("revealjs/js/reveal.min.js");

         // webfonts w/ remote url
         setRemoteWebFonts(&vars);

         // mathjax w/ remote url
         vars["mathjax"] = mathjaxIfRequired(slides);

         // no IDE interaction
         vars["slide_commands"] = "";
         vars["slides_js"] = "";
         vars["init_commands"] = "";

         // width and height
         vars["reveal_width"] = "960";
         vars["reveal_height"] = "700";


         std::istringstream templateStream(presentationTemplate);
         html_utils::Base64ImageFilter imageFilter(dirPath);
         FilePath htmlPath = dirPath.complete(dirPath.stem() + ".html");
         boost::shared_ptr<std::ostream> pOfs;
         Error error = htmlPath.open_w(&pOfs);
         if (error)
         {
            pResponse->setError(error);
            return;
         }
         pOfs->exceptions(std::istream::failbit | std::istream::badbit);
         boost::iostreams::filtering_ostream standaloneStream ;
         text::TemplateFilter standaloneTemplateFilter(vars);
         standaloneStream.push(standaloneTemplateFilter);
         standaloneStream.push(imageFilter);
         standaloneStream.push(*pOfs);
         boost::iostreams::copy(templateStream, standaloneStream, 128);

         // generate preview version

         // linked versions of reveal assets
         vars["reveal_print_pdf_css"] = revealLink("revealjs/css/print/pdf.css",
                                                    kMediaPrint);
         vars["reveal_css"] = revealLink("revealjs/css/reveal.css");
         vars["reveal_theme_css"] = revealLink("revealjs/css/theme/simple.css");
         vars["reveal_head_js"] = revealLink("revealjs/lib/js/head.min.js");
         vars["reveal_js"] = revealLink("revealjs/js/reveal.js");

         // webfonts local
         setLocalWebFonts(&vars);

         // mathjax local
         vars["mathjax"] = mathjaxLocal(vars["mathjax"]);

         // javascript supporting IDE interaction
         vars["slide_commands"] = slideCommands;
         vars["slides_js"] = resourceFiles().get("presentation/slides.js");
         vars["init_commands"] = initCommands;

         // width and height
         vars["reveal_width"] = "revealDetectWidth()";
         vars["reveal_height"] = "revealDetectHeight()";

         templateStream.seekg (0, std::ios::beg);
         std::stringstream previewOutputStream;
         boost::iostreams::filtering_ostream previewStream ;
         text::TemplateFilter previewTemplateFilter(vars);
         standaloneStream.push(previewTemplateFilter);
         previewStream.push(previewTemplateFilter);
         previewStream.push(previewOutputStream);
         boost::iostreams::copy(templateStream, previewStream, 128);

         // return the presentation
         pResponse->setNoCacheHeaders();
         pResponse->setBody(previewOutputStream);
      }
      catch(const std::exception& e)
      {
         pResponse->setError(http::status::InternalServerError,
                             e.what());
         return;
      }
   }

   // special handling for reveal.js assets
   else if (boost::algorithm::starts_with(path, "revealjs/"))
   {
      path = http::util::pathAfterPrefix(request, "/presentation/revealjs/");
      FilePath filePath = presentationResourcesPath().complete("revealjs/" + path);
      pResponse->setFile(filePath, request);
   }

   // special handling for mathjax assets
   else if (boost::algorithm::starts_with(path, "mathjax/"))
   {
      FilePath filePath =
            session::options().mathjaxPath().parent().childPath(path);
      pResponse->setFile(filePath, request);
   }


   // serve the file back
   else
   {
      FilePath targetFile = presentation::state::directory().childPath(path);
      if (!request.headerValue("Range").empty())
      {
         pResponse->setRangeableFile(targetFile, request);
      }
      else
      {
         // indicate that we accept byte range requests
         pResponse->addHeader("Accept-Ranges", "bytes");

         // return the file
         pResponse->setFile(targetFile, request);
      }
   }
}


// we save the most recent /help/presentation/&file=parameter so we
// can resolve relative file references against it. we do this
// separately from presentation::state::directory so that the help
// urls can be available within the help pane (and history)
// independent of the duration of the presentation tab
FilePath s_presentationHelpDir;


} // anonymous namespace

void handlePresentationHelpRequest(const core::http::Request& request,
                                   const std::string& jsCallbacks,
                                   core::http::Response* pResponse)
{
   // check if this is a root request
   std::string file = request.queryParamValue("file");
   if (!file.empty())
   {
      // ensure file exists
      FilePath filePath = module_context::resolveAliasedPath(file);
      if (!filePath.exists())
      {
         pResponse->setError(http::status::NotFound, request.uri());
         return;
      }

      // save the file's directory (for resolving other resources)
      s_presentationHelpDir = filePath.parent();


      // read in the file (process markdown)
      std::string helpDoc;
      Error error = markdown::markdownToHTML(filePath,
                                             markdown::Extensions(),
                                             markdown::HTMLOptions(),
                                             &helpDoc);
      if (error)
      {
         pResponse->setError(error);
         return;
      }

      // process the template
      std::map<std::string,std::string> vars;
      vars["title"] = html_utils::defaultTitle(helpDoc);
      vars["styles"] = resourceFiles().get("presentation/helpdoc.css");
      vars["r_highlight"] = resourceFiles().get("r_highlight.html");
      vars["mathjax"] = mathjaxIfRequired(helpDoc);
      vars["content"] = helpDoc;
      vars["js_callbacks"] = jsCallbacks;
      pResponse->setNoCacheHeaders();
      pResponse->setBody(resourceFiles().get("presentation/helpdoc.html"),
                         text::TemplateFilter(vars));
   }

   // it's a relative file reference
   else
   {
      // make sure the directory exists
      if (!s_presentationHelpDir.exists())
      {
         pResponse->setError(http::status::NotFound,
                             "Directory not found: " +
                             s_presentationHelpDir.absolutePath());
         return;
      }

      // resolve the file reference
      std::string path = http::util::pathAfterPrefix(request,
                                                     "/help/presentation/");

      // serve the file back
      pResponse->setFile(s_presentationHelpDir.complete(path), request);
   }
}



json::Value presentationStateAsJson()
{
   return presentation::state::asJson();
}

Error initialize()
{
   if (session::options().programMode() == kSessionProgramModeServer)
   {
      // register rs_showPresentation
      R_CallMethodDef methodDefShowPresentation;
      methodDefShowPresentation.name = "rs_showPresentation" ;
      methodDefShowPresentation.fun = (DL_FUNC) rs_showPresentation;
      methodDefShowPresentation.numArgs = 3;
      r::routines::addCallMethod(methodDefShowPresentation);

      using boost::bind;
      using namespace session::module_context;
      ExecBlock initBlock ;
      initBlock.addFunctions()
         (bind(registerUriHandler, "/presentation", handlePresentationPaneRequest))
         (bind(registerRpcMethod, "set_presentation_slide_index", setPresentationSlideIndex))
         (bind(registerRpcMethod, "close_presentation_pane", closePresentationPane))
         (bind(presentation::state::initialize))
         (bind(sourceModuleRFile, "SessionPresentation.R"));

      return initBlock.execute();
   }
   else
   {
      return Success();
   }
}

} // namespace presentation
} // namespace modules
} // namesapce session

