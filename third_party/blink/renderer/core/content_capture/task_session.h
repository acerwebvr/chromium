// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CONTENT_CAPTURE_TASK_SESSION_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CONTENT_CAPTURE_TASK_SESSION_H_

#include <vector>

#include "base/memory/scoped_refptr.h"
#include "cc/paint/node_holder.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/heap_allocator.h"
#include "third_party/blink/renderer/platform/heap/member.h"
#include "third_party/blink/renderer/platform/wtf/hash_map.h"

namespace blink {

class ContentHolder;
class Document;
class SentNodes;

// This class wraps the captured content and the detached nodes that need to be
// sent out by the ContentCaptureTask, it has a Document to DocumentSession
// mapping, and all data is grouped by document. There are two sources of data:
//
// One is the captured content which is set by the ContentCaptureTask through
// SetCapturedContent() only if the task session is empty, i.e all data must be
// sent before capturing the on-screen content, the captured content is then
// grouped into DocumentSession.
//
// Another is the detached nodes which are set by the ContentCaptureManager,
// they are saved to the DocumentSession directly.
//
// ContentCaptureTask gets the data per document by using
// GetUnsentDocumentSession() and GetNextUnsentContentHolder(), and must send
// all data out before capturing on-screen content again.
class TaskSession : public GarbageCollectedFinalized<TaskSession> {
 public:
  // This class manages the captured content and the detached nodes per
  // document, the data is moved to the ContentCaptureTask while required. This
  // class has an instance per document, will be released while the associated
  // document is GC-ed, see TaskSession::to_document_session_.
  class DocumentSession : public GarbageCollectedFinalized<DocumentSession> {
   public:
    DocumentSession(const Document& document, SentNodes& sent_nodes);
    ~DocumentSession();
    void AddNodeHolder(cc::NodeHolder node_holder);
    void AddDetachedNode(int64_t id);
    bool HasUnsentData() const {
      return HasUnsentCapturedContent() || HasUnsentDetachedNodes();
    }
    bool HasUnsentCapturedContent() const { return !captured_content_.empty(); }
    bool HasUnsentDetachedNodes() const { return !detached_nodes_.empty(); }
    std::vector<int64_t> MoveDetachedNodes();
    const Document* GetDocument() const { return document_; }
    bool FirstDataHasSent() const { return first_data_has_sent_; }
    void SetFirstDataHasSent() { first_data_has_sent_ = true; }

    // Removes the unsent node from |captured_content_|, and returns it as
    // ContentHolder.
    scoped_refptr<ContentHolder> GetNextUnsentContentHolder();

    // Resets the |captured_content_| and the |detached_nodes_|, shall only be
    // used if those data doesn't need to be sent, e.g. there is no
    // WebContentCaptureClient for this document.
    void Reset();

    void Trace(blink::Visitor*);

   private:
    // The captured content that belongs to this document.
    std::vector<cc::NodeHolder> captured_content_;
    // The list of content id of node that has been detached from the
    // LayoutTree.
    std::vector<int64_t> detached_nodes_;
    WeakMember<const Document> document_;
    Member<SentNodes> sent_nodes_;
    bool first_data_has_sent_ = false;
    // This is for the metrics to record the total node that has been sent.
    size_t total_sent_nodes_ = 0;
  };

  TaskSession(SentNodes& sent_nodes);

  // Returns the DocumentSession that hasn't been sent.
  DocumentSession* GetNextUnsentDocumentSession();

  // This can only be invoked when all data has been sent (i.e. HasUnsentData()
  // returns False).
  void SetCapturedContent(const std::vector<cc::NodeHolder>& captured_content);

  void OnNodeDetached(const cc::NodeHolder& node_holder);

  bool HasUnsentData() const { return has_unsent_data_; }

  void Trace(blink::Visitor*);

 private:
  void GroupCapturedContentByDocument(
      const std::vector<cc::NodeHolder>& captured_content);
  DocumentSession& EnsureDocumentSession(const Document& doc);
  DocumentSession* GetDocumentSession(const Document& document) const;
  const Node* GetNodeIf(bool sent, const cc::NodeHolder& node_holder) const;

  Member<SentNodes> sent_nodes_;

  // This owns the DocumentSession which is released along with Document.
  HeapHashMap<WeakMember<const Document>, Member<DocumentSession>>
      to_document_session_;

  // Because the captured content and the detached node are in the
  // DocumentSession, this is used to avoid to iterate all document sessions
  // to find out if there is any of them.
  bool has_unsent_data_ = false;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CONTENT_CAPTURE_TASK_SESSION_H_
