/**
 * @brief recorder的前置声明
 * @author 陈志(chenzhi@af.local)
 */
#ifndef AMI_RECORD_FWD_H_
#define AMI_RECORD_FWD_H_

namespace ami
{

class RecordAgent;
class RecordClient;
class AsyncRecordClient;
class RecordChannel;
class TxRecordChannel;
class RxRecordChannel;
class StRecordChannel;
class ControlConnection;
class ControlClient;
class ControlServer;
class MessageTrack;
class TxMessageTrack;
class RxMessageTrack;
class StMessageTrack;
class Recorder;
class RecordReader;
class SerialWorker;
template <typename TrackType>
class RecordIterator;
template <typename TrackType, typename KeyindexType>
class KeyindexRecordIterator;

}  // namespace ami

#endif /* AMI_RECORD_FWD_H_ */
