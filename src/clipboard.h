#pragma once

#include <QMimeData>
#include <memory>

namespace snittClipboard {
// Publishes image and/or plain-text data; other MIME formats are rejected.
// Takes ownership even on failure and confirms publication before callers close
// the document. Windows eagerly publishes PNG/DIBV5 or Unicode text while locked.
bool publish(std::unique_ptr<QMimeData> data);
}
