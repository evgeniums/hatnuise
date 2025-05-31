/*
    Copyright (c) 2020 - current, Evgeny Sidorov (decfile.com), All rights reserved.
    
    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)
      
*/

/****************************************************************************/
/*
    
*/
/** @file hatnuise/hatnuise.h
  *
  *  Uise library binding hatn and uise frameworks.
  *
  */

/****************************************************************************/

#ifndef HATNUISE_H
#define HATNUISE_H

#include <hatn/common/visibilitymacros.h>

#ifndef HATN_UISE_EXPORT
#   ifdef HATN_UISE_BUILD
#       define HATN_UISE_EXPORT HATN_VISIBILITY_EXPORT
#   else
#       define HATN_UISE_EXPORT HATN_VISIBILITY_IMPORT
#   endif
#endif

#define HATN_UISE_NAMESPACE_BEGIN namespace hatnuise {
#define HATN_UISE_NAMESPACE_END }

#define HATN_UISE_NAMESPACE hatnuise
#define HATN_UISE_NS uise
#define HATN_UISE_USING using namespace hatnuise;

#endif // HATNUISE_H
