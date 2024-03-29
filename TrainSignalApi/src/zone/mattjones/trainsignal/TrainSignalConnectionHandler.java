/**
 * File: TrainSignalConnectionHandler.java
 * Author: Matt Jones
 * Date: 7.11.2018
 * Desc: The handler that waits for an incoming connection from the train signal so that it can
 *          begin sending commands to it. This class only handles a single connection. If a new
 *       connection is made, the existing client is disconnected.
 */

package zone.mattjones.trainsignal;

import zone.mattjones.common.scheduler.ThreadScheduler;

import java.io.IOException;
import java.io.InputStream;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ConcurrentLinkedQueue;

public class TrainSignalConnectionHandler extends Thread {
    /** The maximum number of messages that can be queued up for a connected signal. */
    private static final int MAX_QUEUE_SIZE = 5;

    /** The most we're willing to read as feedback from the signal. */
    private static final int MAX_READ_SIZE_BYTES = 64;

    /** The maximum amount of time to block on a read request from the signal in ms. */
    private static final int READ_TIMEOUT_MS = 2000;

    /** The amount of time between ping pong messages to make sure the signal is still alive. */
    private static final long PING_PONG_INTERVAL_MS = 10000;

    /** The port for the server to run on. */
    private final int mPort;

    /** A means of scheduling a recurring task for this thread. */
    private final ThreadScheduler mScheduler;
    
    /** The socket listening for incoming connections. */
    private ServerSocket mServerSocket;

    /** The ID of the scheduled task playing ping pong with the signal to make sure it's alive. */
    private long mPingPongTaskId;
    
    /** The client (train signal arduino) currently connected to the server. */
    private Socket mActiveClientSocket;
    
    /** Messages waiting to be sent to the train signal. */
    // TODO(Matt): This probably doesn't need to be a list since messages are sent immediately. The
    //             only reason to keep this would be for diagnostic messages.
    private ConcurrentLinkedQueue<byte[]> mMessages;
    
    /** Whether the server should stop on the next iteration. */
    private boolean mStopServer;
    
    /** Default constructor. */
    public TrainSignalConnectionHandler(int serverPort, ThreadScheduler scheduler) {
        mPort = serverPort;
        mScheduler = scheduler;
        mMessages = new ConcurrentLinkedQueue<>();

        mPingPongTaskId = mScheduler.scheduleTask(() -> {
            if (mActiveClientSocket != null && !mActiveClientSocket.isClosed()) {
                addMessage(TrainSignalMessage.ACK_MESSAGE);
            }
        }, PING_PONG_INTERVAL_MS, true);

        start();
    }
    
    @Override
    public void run() {
        while (!mStopServer) {
            // If something happened to the server socket, reinitialize it.
            if (mServerSocket == null || mServerSocket.isClosed()) {
                try {
                    mServerSocket = new ServerSocket(mPort);
                } catch (IOException ex) {
                    mServerSocket = null;

                    System.err.println(
                            "[error]: Failed to create server socket: " + ex.getMessage());
                    try {
                        Thread.sleep(5000);
                    } catch (InterruptedException e) {
                        System.err.println(
                                "[info]: Sleep interrupted for server socket reconnect: " +
                                        ex.getMessage());
                    }
                }
            }

            if (mServerSocket == null) {
                continue;
            }

            try {
                // Block until a connection is made. Currently, it's only possible to run with a
                // single connected client.
                mActiveClientSocket = mServerSocket.accept();
                mActiveClientSocket.setSoTimeout(READ_TIMEOUT_MS);

                while (mActiveClientSocket.isConnected() && !mActiveClientSocket.isClosed()) {
                    sendMessages();

                    // Sleep for one minute. A message being added to the queue will interrupt this.
                    try {
                        Thread.sleep(60000);
                    } catch (InterruptedException e) {
                        System.err.println("[info]: Messaging thread interrupted - a message was "
                                + "likely enqueued.");
                    }
                }

            } catch (IOException e) {
                System.err.println("[error]: Messaging thread exception: " + e.getMessage());
            } finally {
                if (!mActiveClientSocket.isClosed()) {
                    try {
                        mActiveClientSocket.close();
                    } catch (IOException ex) {
                        System.err.println(
                                "[error]: Failed to close signal socket: " + ex.getMessage());
                    }
                }
                mActiveClientSocket = null;
            }
        }
    }
    
    /**
     * Send any messages in the queue to the client.
     */
    private void sendMessages() throws IOException {
        if (mMessages.isEmpty() || mActiveClientSocket == null
                || !mActiveClientSocket.isConnected()) {
            return;
        }

        while (!mMessages.isEmpty()) {
            byte[] curMessage = mMessages.poll();

            mActiveClientSocket.getOutputStream().write(curMessage);
            mActiveClientSocket.getOutputStream().flush();

            // Expect an acknowledgement before trying to send the next message.
            byte[] buffer = new byte[16];
            int size;
            int totalBytesRead = 0;
            String message = "";
            InputStream in = mActiveClientSocket.getInputStream();
            while ((size = in.read(buffer, 0, buffer.length)) >= 0) {
                totalBytesRead += size;
                if (totalBytesRead > MAX_READ_SIZE_BYTES) {
                    // TODO(Matt): Consider disconnecting here.
                    break;
                }
                message += new String(buffer, 0, size, Charset.defaultCharset());
                if (TrainSignalMessage.isTerminatedMessage(message.getBytes(StandardCharsets.US_ASCII))) {
                    break;
                }
            }

            if (!TrainSignalMessage.isAckMessage(message.getBytes(StandardCharsets.US_ASCII))) {
                throw new IOException("Did not receive expected ack from signal!");
            }
        }
    }
    
    /**
     * Add a message to the queue. If a client is connected, the message is sent immediately.
     * @param message The message to send to the client.
     * @return Whether the operation was successful.
     */
    public boolean addMessage(byte[] message) {
        // If no client is connected, only allow one message (the most recent) to be added to the
        // queue. This will prevent a message flood (and memory leak) if the signal disconnects and
        // messages continue to be added.
        if (mActiveClientSocket == null || !mActiveClientSocket.isConnected() ||
                mActiveClientSocket.isClosed()) {
            mMessages.clear();
            mMessages.add(message);
            System.err.println("[warning]: Message added while signal disconnected. Only the most "
                    + "recent message will be sent once connected.");
            return true;
        }

        if (mMessages.size() >= MAX_QUEUE_SIZE) {
            System.err.println("[error]: Signal message queue size exceeded! Ignoring message... ");
            return false;
        }

        mMessages.add(message);
        interrupt();

        return true;
    }
    
    /**
     * Cause the server to close the connection and reopen it.
     * @throws IOException
     */
    public void resetServer() throws IOException {
        if (mServerSocket != null) mServerSocket.close();
        if (mActiveClientSocket != null) mActiveClientSocket.close();
    }
    
    /**
     * Stops the server socket and exits the thread.
     * @throws IOException
     */
    public void killServer() throws IOException {
        mStopServer = true;
        if (mScheduler != null) {
            mScheduler.cancelTask(mPingPongTaskId);
        }
        resetServer();
    }
}
