#include <stdlib.h>

#include "split.h"

#include "memfun.h"
#include "buffer.h"
#include "handle.h"
#include "debug.h"
#include "collectors.h"
#include "threading.h"
#include "stream_layer.h"
#include "list.h"

#ifdef DISTRIBUTED_SNET
#include "routing.h"
#include "fun.h"
#endif /* DISTRIBUTED_SNET */

/* ------------------------------------------------------------------------- */
/*  SNetSplit                                                                */
/* ------------------------------------------------------------------------- */




snet_blist_elem_t *FindBufInList(snet_util_list_t *list, int num) {
  snet_blist_elem_t *elem;
  snet_util_list_iter_t *current_position;

  current_position = SNetUtilListFirst(list);
  while(SNetUtilListIterCurrentDefined(current_position)) {
    elem = (snet_blist_elem_t*)SNetUtilListIterGet(current_position);

    if( elem->num == num) {
      SNetUtilListIterDestroy(current_position);
      return elem;
    }
    else {
      current_position = SNetUtilListIterNext(current_position);
    }
  }
  SNetUtilListIterDestroy(current_position);
  return(NULL);
}

static void *SplitBoxThread( void *hndl) {
  snet_util_list_iter_t *current_position;

  int i;
  snet_handle_t *hnd = (snet_handle_t*)hndl;
  snet_tl_stream_t *initial;
  int ltag, ltag_val, utag, utag_val;
  snet_record_t *rec, *current_sort_rec;
  snet_startup_fun_t boxfun;
  bool terminate = false;
  snet_util_list_t *repos = NULL;
  snet_blist_elem_t *elem;

#ifdef DISTRIBUTED_SNET
  int node_id;
  snet_fun_id_t fun_id;
  snet_info_t *info;
  snet_tl_stream_t *temp_stream;
#endif /* DISTRIBUTED_SNET */

  initial = SNetHndGetOutput( hnd);
  boxfun = SNetHndGetBoxfun( hnd);
  ltag = SNetHndGetTagA( hnd);
  utag = SNetHndGetTagB( hnd);

#ifdef DISTRIBUTED_SNET
  node_id = SNetIDServiceGetNodeID();

  if(!SNetDistFunFun2ID(boxfun, &fun_id)) {
    /* TODO: This is an error!*/
  }
#endif /* DISTRIBUTED_SNET */

  while( !( terminate)) {
    rec = SNetTlRead( SNetHndGetInput( hnd));

    switch( SNetRecGetDescriptor( rec)) {
      case REC_data:
        ltag_val = SNetRecGetTag( rec, ltag);
        utag_val = SNetRecGetTag( rec, utag);
#ifdef DEBUG_SPLIT
        SNetUtilDebugNotice("SPLIT got data");
#endif
        if( repos == NULL) {
          elem = SNetMemAlloc( sizeof( snet_blist_elem_t));
          elem->num = ltag_val;
          elem->stream = SNetTlCreateStream( BUFFER_SIZE);

#ifdef DISTRIBUTED_SNET
	  info = SNetInfoInit();

	  if(SNetHndIsSplitByLocation( hnd)) {

	    SNetInfoSetRoutingContext(info, SNetRoutingContextInit(SNetRoutingGetNewID(), true, node_id, &fun_id, ltag_val));
	     
	    temp_stream = boxfun(elem->stream, info, ltag_val);    

	    temp_stream = SNetRoutingContextEnd(SNetInfoGetRoutingContext(info), temp_stream);
	     
	  } else {
	    SNetInfoSetRoutingContext(info, SNetRoutingContextInit(SNetRoutingGetNewID(), true, node_id, &fun_id, node_id));
	     
	    temp_stream = boxfun(elem->stream, info, node_id);    

	    temp_stream = SNetRoutingContextEnd(SNetInfoGetRoutingContext(info), temp_stream);
	  }	 
	  
	  if(temp_stream != NULL) {
	    SNetTlWrite( initial, SNetRecCreate( REC_collect, temp_stream));
	  }

	  SNetInfoDestroy(info);
#else
          SNetTlWrite( initial, 
                       SNetRecCreate( REC_collect, boxfun( elem->stream)));
#endif /* DISTRIBUTED_SNET */

          //SNetBufBlockUntilEmpty( initial);
          repos = SNetUtilListCreate();
          repos = SNetUtilListAddBeginning(repos, elem);
        }

        for( i = ltag_val; i <= utag_val; i++) {
          elem = FindBufInList( repos, i);
          if( elem == NULL) {
            elem = SNetMemAlloc( sizeof( snet_blist_elem_t));
            elem->num = i;
            elem->stream = SNetTlCreateStream( BUFFER_SIZE);


            repos = SNetUtilListAddBeginning(repos, elem);

#ifdef DISTRIBUTED_SNET
	    info = SNetInfoInit();

	    if(SNetHndIsSplitByLocation( hnd)) {
	      SNetInfoSetRoutingContext(info, SNetRoutingContextInit(SNetRoutingGetNewID(), true, node_id, &fun_id, i));
	      
	      temp_stream = boxfun(elem->stream, info, i);    
	      
	      temp_stream = SNetRoutingContextEnd(SNetInfoGetRoutingContext(info), temp_stream);
	      
	    } else {
	      SNetInfoSetRoutingContext(info, SNetRoutingContextInit(SNetRoutingGetNewID(), true, node_id, &fun_id, node_id));
	      
	      temp_stream = boxfun(elem->stream, info, node_id);    
	      
	      temp_stream = SNetRoutingContextEnd(SNetInfoGetRoutingContext(info), temp_stream);
	    }

	    if(temp_stream != NULL) {
	      SNetTlWrite( initial, SNetRecCreate( REC_collect, temp_stream));
	    }
	    
	    SNetInfoDestroy(info);
#else
	    SNetTlWrite( initial, 
                         SNetRecCreate( REC_collect, boxfun( elem->stream)));
#endif /* DISTRIBUTED_SNET */

          } 
          if( i == utag_val) {
            SNetTlWrite( elem->stream, rec); // last rec is not copied.
          }
          else {
            SNetTlWrite( elem->stream, SNetRecCopy( rec)); // COPY
          }
        } 
        break;
      case REC_sync:
        SNetHndSetInput( hnd, SNetRecGetStream( rec));
        SNetRecDestroy( rec);

        break;
      case REC_collect:
#ifdef DEBUG_SPLIT
        SNetUtilDebugNotice("[SPLIT] Unhandled control record,"
                            " destroying it \n\n");
#endif
        SNetRecDestroy( rec);
        break;
      case REC_sort_begin:
        current_sort_rec = rec;
        if(!SNetUtilListIsEmpty(repos)) {
          current_position = SNetUtilListFirst(repos);
          while(SNetUtilListIterHasNext(current_position)) {
            elem = SNetUtilListIterGet(current_position);
            SNetTlWrite( elem->stream,
                SNetRecCreate( REC_sort_begin, SNetRecGetLevel( rec),
                               SNetRecGetNum( rec)));
            current_position = SNetUtilListIterNext(current_position);
          }
          elem = SNetUtilListIterGet(current_position);
          SNetTlWrite(elem->stream, rec);
          SNetUtilListIterDestroy(current_position);
        } else {
          /* we are unable to relay this record anyway, so we discard it? */
#ifdef DEBUG_SPLIT
          SNetUtilDebugNotice("[SPLIT] Got sort_begin_record with nowhere to"
                     " send it!");
#endif
          SNetRecDestroy(rec);
        }
        break;
      case REC_sort_end:
        current_sort_rec = rec;
        if(!SNetUtilListIsEmpty(repos)) {
          current_position = SNetUtilListFirst(repos);
          while(SNetUtilListIterHasNext(current_position)) {
            elem = SNetUtilListIterGet(current_position);
            SNetTlWrite( elem->stream,
			 SNetRecCreate( REC_sort_end, SNetRecGetLevel( rec),
                               SNetRecGetNum( rec)));
            current_position = SNetUtilListIterNext(current_position);
          }
          elem = SNetUtilListIterGet(current_position);
          SNetTlWrite(elem->stream, rec);
          SNetUtilListIterDestroy(current_position);
        } else {
#ifdef DEBUG_SPLIT
          SNetUtilDebugNotice("[SPLIT] Got sort_end_record with nowhere to"
                      " send it!");
#endif
          SNetRecDestroy(rec);
        }
        break;
      case REC_terminate:
        terminate = true;

	      if(repos != NULL) {
	        while(!SNetUtilListIsEmpty(repos)) {
	          current_position = SNetUtilListFirst(repos);
	          elem = SNetUtilListIterGet(current_position);
	          SNetTlWrite(elem->stream, SNetRecCopy( rec));

	          SNetTlMarkObsolete(elem->stream);
	          SNetMemFree(elem);

	          repos = SNetUtilListIterDelete(current_position);
	        } 

          SNetUtilListIterDestroy(current_position);
          SNetUtilListDestroy(repos);
          //SNetRecDestroy(rec);
        }

        SNetTlWrite(initial, rec);
        break;
      case REC_probe:
        current_position = SNetUtilListFirst(repos);
        elem = SNetUtilListIterGet(current_position);
        SNetTlWrite(elem->stream, rec);
        current_position = SNetUtilListIterNext(current_position);
        while(SNetUtilListIterCurrentDefined(current_position)) {
          elem = SNetUtilListIterGet(current_position);
          SNetTlWrite(elem->stream, SNetRecCopy(rec));
          current_position = SNetUtilListIterNext(current_position);
        }
      break;
    default:
      SNetUtilDebugNotice("[Split] Unknown control record destroyed (%d).\n", SNetRecGetDescriptor( rec));
      SNetRecDestroy( rec);
      break;
    }
  }

  SNetTlMarkObsolete(initial);
  SNetHndDestroy( hnd);
  return( NULL);
}



extern snet_tl_stream_t *SNetSplit( snet_tl_stream_t *input,
#ifdef DISTRIBUTED_SNET
				    snet_info_t *info, 
				    int location,
#endif /* DISTRIBUTED_SNET */
				    snet_startup_fun_t box_a,
				    int ltag, int utag) 
{
  snet_tl_stream_t *initial, *output;
  snet_handle_t *hnd;

#ifdef DISTRIBUTED_SNET
  input = SNetRoutingContextUpdate(SNetInfoGetRoutingContext(info), input, location);

  if(location == SNetIDServiceGetNodeID()) {

#ifdef DISTRIBUTED_DEBUG
    SNetUtilDebugNotice("Split created");
#endif /* DISTRIBUTED_DEBUG */

#endif /* DISTRIBUTED_SNET */
  
    initial = SNetTlCreateStream( BUFFER_SIZE);
    hnd = SNetHndCreate( HND_split, input, initial, box_a, ltag, utag, false);
    
    output = CreateCollector(initial);
    
    SNetTlCreateComponent(SplitBoxThread, (void*)hnd, ENTITY_split_nondet);
    
#ifdef DISTRIBUTED_SNET
  } else { 
    output = input; 
  }
#endif /* DISTRIBUTED_SNET */

  return(output);
}

#ifdef DISTRIBUTED_SNET
extern snet_tl_stream_t *SNetLocSplit(snet_tl_stream_t *input,
				      snet_info_t *info, 
				      int location,
				      snet_startup_fun_t box_a,
				      int ltag, int utag)
{
  snet_tl_stream_t *initial, *output;
  snet_handle_t *hnd;

  input = SNetRoutingContextUpdate(SNetInfoGetRoutingContext(info), input, location); 

  if(location == SNetIDServiceGetNodeID()) {

#ifdef DISTRIBUTED_DEBUG
    SNetUtilDebugNotice("LocSplit created");
#endif /* DISTRIBUTED_DEBUG */

    initial = SNetTlCreateStream( BUFFER_SIZE);
    hnd = SNetHndCreate( HND_split, input, initial, box_a, ltag, utag, true);
    
    output = CreateCollector(initial);
    
    SNetTlCreateComponent(SplitBoxThread, (void*)hnd, ENTITY_split_nondet);
    
  } else { 
    output = input; 
  }

  return(output);
}
#endif /* DISTRIBUTED_SNET */


static void *DetSplitBoxThread( void *hndl) {
  int i;
  snet_handle_t *hnd = (snet_handle_t*)hndl;
  snet_tl_stream_t *initial, *tmp;
  int ltag, ltag_val, utag, utag_val;
  snet_record_t *rec;
  snet_startup_fun_t boxfun;
  bool terminate = false;
  snet_util_list_t *repos = NULL;
  snet_blist_elem_t *elem;
  snet_util_list_iter_t *current_position;
  int counter = 1;

#ifdef DISTRIBUTED_SNET
  int node_id;
  snet_fun_id_t fun_id;
  snet_info_t *info;
  snet_tl_stream_t *temp_stream;
#endif /* DISTRIBUTED_SNET */

  initial = SNetHndGetOutput(hnd);
  boxfun = SNetHndGetBoxfun(hnd);
  ltag = SNetHndGetTagA(hnd);
  utag = SNetHndGetTagB(hnd);

#ifdef DISTRIBUTED_SNET
  node_id = SNetIDServiceGetNodeID();

  if(!SNetDistFunFun2ID(boxfun, &fun_id)) {
    /* TODO: This is an error!*/
  }
#endif /* DISTRIBUTED_SNET */

  while( !( terminate)) {
    rec = SNetTlRead(SNetHndGetInput( hnd));

    switch( SNetRecGetDescriptor( rec)) {
      case REC_data:
        ltag_val = SNetRecGetTag( rec, ltag);
        utag_val = SNetRecGetTag( rec, utag);

        if( repos == NULL) {
          elem = SNetMemAlloc( sizeof( snet_blist_elem_t));
          elem->num = ltag_val;
          elem->stream = SNetTlCreateStream(BUFFER_SIZE);
          SNetTlWrite(initial, SNetRecCreate(REC_sort_begin, 0, 0));

#ifdef DISTRIBUTED_SNET
	  info = SNetInfoInit();

	  if(SNetHndIsSplitByLocation( hnd)) {
	    SNetInfoSetRoutingContext(info, SNetRoutingContextInit(SNetRoutingGetNewID(), true, node_id, &fun_id, ltag_val));
	    
	    temp_stream = boxfun(elem->stream, info, ltag_val);    
	    
	    temp_stream = SNetRoutingContextEnd(SNetInfoGetRoutingContext(info), temp_stream);
	    
	  } else {
	    SNetInfoSetRoutingContext(info, SNetRoutingContextInit(SNetRoutingGetNewID(), true, node_id, &fun_id, node_id));
	    
	    temp_stream = boxfun(elem->stream, info, node_id);    
	    
	    temp_stream = SNetRoutingContextEnd(SNetInfoGetRoutingContext(info), temp_stream);
	  }

	  
	  if(temp_stream != NULL) {
	    SNetTlWrite( initial, SNetRecCreate( REC_collect, temp_stream));
	  }

	  SNetInfoDestroy(info);
#else
          tmp = boxfun(elem->stream);
          SNetTlWrite(initial, SNetRecCreate(REC_collect, tmp));
#endif /* DISTRIBUTED_SNET */

//          SNetBufPut(tmp, SNetRecCreate( REC_sort_begin, 0, 0));
          SNetTlWrite(tmp, SNetRecCreate(REC_sort_end, 0, 0));

          SNetTlWrite(initial, SNetRecCreate(REC_sort_end, 0, 0));
          repos = SNetUtilListCreate();
          repos = SNetUtilListAddBeginning(repos, elem);
        }


        SNetTlWrite(initial, SNetRecCreate( REC_sort_begin, 0, counter));
        current_position = SNetUtilListFirst(repos);
        while(SNetUtilListIterCurrentDefined(current_position)) {
          elem = SNetUtilListIterGet(current_position);
          SNetTlWrite(elem->stream, 
                      SNetRecCreate( REC_sort_begin, 0, counter));
          current_position = SNetUtilListIterNext(current_position);
        }

        for( i = ltag_val; i <= utag_val; i++) {
          elem = FindBufInList(repos, i);
          if(elem == NULL) {
            elem = SNetMemAlloc( sizeof( snet_blist_elem_t));
            elem->num = i;
            elem->stream = SNetTlCreateStream(BUFFER_SIZE);
            repos = SNetUtilListAddBeginning(repos, elem);
#ifdef DISTRIBUTED_SNET
	    info = SNetInfoInit();

	    if(SNetHndIsSplitByLocation( hnd)) {
	      SNetInfoSetRoutingContext(info, SNetRoutingContextInit(SNetRoutingGetNewID(), true, node_id, &fun_id, i));
	      
	      temp_stream = boxfun(elem->stream, info, i);    
	      
	      temp_stream = SNetRoutingContextEnd(SNetInfoGetRoutingContext(info), temp_stream);
	      
	    } else {
	      SNetInfoSetRoutingContext(info, SNetRoutingContextInit(SNetRoutingGetNewID(), true, node_id, &fun_id, node_id));
	      
	      temp_stream = boxfun(elem->stream, info, node_id);    
	      
	      temp_stream = SNetRoutingContextEnd(SNetInfoGetRoutingContext(info), temp_stream);
	    }

	    if(temp_stream != NULL) {
	      SNetTlWrite( initial, SNetRecCreate( REC_collect, temp_stream));
	    }

	    SNetInfoDestroy(info);
#else
            SNetTlWrite( initial, SNetRecCreate( REC_collect, boxfun( elem->stream)));
#endif /* DISTRIBUTED_SNET */
          }
          if( i == utag_val) {
            SNetTlWrite(elem->stream, rec); // last rec is not copied.
          }
          else {
            SNetTlWrite(elem->stream, SNetRecCopy(rec)); // COPY
          }
        }

        current_position = SNetUtilListFirst(repos);
        while(SNetUtilListIterCurrentDefined(current_position)) {
          elem = SNetUtilListIterGet(current_position);
          SNetTlWrite(elem->stream, SNetRecCreate( REC_sort_end, 0, counter));
          current_position = SNetUtilListIterNext(current_position);
        }
        SNetTlWrite(initial, SNetRecCreate( REC_sort_end, 0, counter));
        counter += 1;

        break;
      case REC_sync:
        SNetHndSetInput( hnd, SNetRecGetStream( rec));
        SNetRecDestroy( rec);
        break;
      case REC_collect:
#ifdef SPLIT_DEBUG
        SNetUtilDebugNotice("[SPLIT] Unhandled control record," 
                            " destroying it\n\n");
#endif
        SNetRecDestroy( rec);
        break;
      case REC_sort_begin:
        SNetRecSetLevel( rec, SNetRecGetLevel( rec) + 1);
        SNetTlWrite(SNetHndGetOutput(hnd), rec);
        break;
      case REC_sort_end:
        SNetRecSetLevel( rec, SNetRecGetLevel( rec) + 1);
        SNetTlWrite(SNetHndGetOutput( hnd), rec);
        break;
      case REC_terminate:
        terminate = true;
        current_position = SNetUtilListFirst(repos);
        while(SNetUtilListIterCurrentDefined(current_position)) {
          elem = SNetUtilListIterGet(current_position);
          SNetTlWrite(elem->stream, 
                      SNetRecCreate( REC_sort_begin, 0, counter));
          SNetTlWrite(elem->stream, SNetRecCopy(rec));

	  SNetTlMarkObsolete(elem->stream);
	  SNetMemFree(elem);

          current_position = SNetUtilListIterNext(current_position);
        }
        SNetUtilListIterDestroy(current_position);
        SNetUtilListDestroy(repos);

        SNetTlWrite(initial, SNetRecCreate( REC_sort_begin, 0, counter));
        SNetTlWrite(initial, rec);
        break;
      case REC_probe:
        current_position = SNetUtilListFirst(repos);
        elem = SNetUtilListIterGet(current_position);
        SNetTlWrite(elem->stream, SNetRecCreate(REC_sort_begin, 0, counter));
        SNetTlWrite(elem->stream, rec);
        while(SNetUtilListIterCurrentDefined(current_position)) {
          elem = SNetUtilListIterGet(current_position);
          SNetTlWrite(elem->stream, SNetRecCreate(REC_sort_begin, 0, counter));
          SNetTlWrite(elem->stream, SNetRecCopy(rec));
          current_position = SNetUtilListIterNext(current_position);
        }
	break;
    default:
      SNetUtilDebugNotice("[Split] Unknown control record destroyed (%d).\n", SNetRecGetDescriptor( rec));
      SNetRecDestroy( rec);
      break;
    }
  }

  SNetTlMarkObsolete(initial);
  SNetHndDestroy( hnd);
  return( NULL);
}



extern snet_tl_stream_t *SNetSplitDet( snet_tl_stream_t *input, 
#ifdef DISTRIBUTED_SNET
				       snet_info_t *info, 
				       int location,
#endif /* DISTRIBUTED_SNET */
				       snet_startup_fun_t box_a,
                                       int ltag, 
                                       int utag) 
{

  snet_tl_stream_t *initial, *output;
  snet_handle_t *hnd;

#ifdef DISTRIBUTED_SNET
  input = SNetRoutingContextUpdate(SNetInfoGetRoutingContext(info), input, location); 

  if(location == SNetIDServiceGetNodeID()) {

#ifdef DISTRIBUTED_DEBUG
    SNetUtilDebugNotice("DetSplit created");
#endif /* DISTRIBUTED_DEBUG */

#endif /* DISTRIBUTED_SNET */
    
    initial = SNetTlCreateStream( BUFFER_SIZE);
    hnd = SNetHndCreate( HND_split, input, initial, box_a, ltag, utag, false);
    
    output = CreateDetCollector( initial);
    
    SNetThreadCreate( DetSplitBoxThread, (void*)hnd, ENTITY_split_det);
    
#ifdef DISTRIBUTED_SNET
  } else { 
    output = input; 
  }
#endif /* DISTRIBUTED_SNET */

  return( output);
}

#ifdef DISTRIBUTED_SNET
snet_tl_stream_t *SNetLocSplitDet(snet_tl_stream_t *input,
				  snet_info_t *info, 
				  int location,
				  snet_startup_fun_t box_a,
				  int ltag,
				  int utag)
{
  snet_tl_stream_t *initial, *output;
  snet_handle_t *hnd;

  input = SNetRoutingContextUpdate(SNetInfoGetRoutingContext(info), input, location); 
							  
  if(location == SNetIDServiceGetNodeID()) {

#ifdef DISTRIBUTED_DEBUG
    SNetUtilDebugNotice("DetLocSplit created");
#endif /* DISTRIBUTED_DEBUG */


    initial = SNetTlCreateStream( BUFFER_SIZE);
    hnd = SNetHndCreate( HND_split, input, initial, box_a, ltag, utag, true);
    
    output = CreateDetCollector( initial);
    
    SNetThreadCreate( DetSplitBoxThread, (void*)hnd, ENTITY_split_det);
    
  } else { 
    output = input; 
  }

  return( output);
}

#endif /* DISTRIBUTED_SNET */
