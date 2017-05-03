/*This is the CPP file for performing Huffman Encoding.
 *It includes functions for compressing and decompressing
 *text files, as well as helper functions to aid in the
 *process.
 *
 * Author: Lawrence Jacobs
 *
 */

#include "Encoding.h"
#include "priorityqueue.h"
#include "HuffmanNode.h"
#include "istream"
#include "strlib.h"
#include "filelib.h"



/*
 * This function builds a frequency table from the input
 *
 * @param: istream passed by reference
 * @return: map of int to int
 *
 */
Map<int, int> buildFrequencyTable(istream& input) {
    Map<int, int> table;
    char ch;

    while(input.get(ch)){
        table[ch]++;    //keep track of the freq. of each char
    }

    table[PSEUDO_EOF]++;    //add EOF char to table, w/freq. of 1
    return table;
}

/*
 * Recursive helper function for building an encoding tree to be
 * used to assign binary representations to each char from the
 * frequency table
 *
 * @param: map of int to int
 * @return: HuffmanNode pointer
 *
 */
HuffmanNode* buildTree(PriorityQueue<HuffmanNode*>& queue){
    if(queue.size() == 0) return nullptr;
    if(queue.size() == 1) return queue.peek();  //1st node points to whole tree

    HuffmanNode* parent = new HuffmanNode;
    HuffmanNode* child1 = queue.dequeue();
    HuffmanNode* child2 = queue.dequeue();

    parent->character = NOT_A_CHAR;     //parent nodes do not contain char's
    parent->count = child1->count + child2->count;  //parent freq = sum of children's
    parent->zero = child1;  //connect parent node with child 1
    parent->one = child2;   //connect parent node with child 2
    queue.enqueue(parent, parent->count);   //enqueue the parent, priority = its "count"
    buildTree(queue);   //continue recursively building tree
    return queue.front();   //1st node points to whole tree
}

/*
 * This function builds an encoding tree to be used to
 * assign binary representations to each char from the
 * frequency table
 *
 * @param: map of int to int
 * @return: HuffmanNode pointer
 *
 */
HuffmanNode* buildEncodingTree(const Map<int, int>& freqTable) {
    PriorityQueue<HuffmanNode*> queue;
    Vector<int> keys = freqTable.keys();

    for(int key: keys){ //for each char (key) in frequency table
        HuffmanNode* node = new HuffmanNode;    //create a new node
        node->character = key;  //assign it to the node
        node->count = freqTable[key];   //save its count to node
        node->zero = nullptr;   //point it at null pointers
        node->one = nullptr;
        queue.enqueue(node, node->count);   //enqueue node w/priority = freq.
    }

    return buildTree(queue);    //connect nodes using buildTree function
}

/*
 * This function frees the encoding tree once we're done with it
 *
 * @param: HuffmanNode pointer
 *
 */
void freeTree(HuffmanNode* node) {
    if(node == nullptr) return;

    freeTree(node->zero);   //address child1 node
    freeTree(node->one);    //address child2 node
    delete node;            //delete parent node
}

/*
 *
 * This is the recursive function that builds encoding map
 *
 * @param  a map from int to string, HuffmanNode pointer, string
 * @return string
 *
 */
void buildMapRec(Map<int, string>& map, HuffmanNode* node, string encoding){
    if(node == nullptr) return;

    buildMapRec(map, node->zero, encoding + "0");   //address child1
    buildMapRec(map, node->one, encoding + "1");    //address child2

    if(node->character != NOT_A_CHAR){              //if at end of tree
        map[node->character] = encoding;  //add character and bit string to map
    }

    return;
}

/*
 *
 * This wrapper function builds encoding map
 *
 * @param  HuffmanNode pointer
 * @return a map from int to string
 *
 */
Map<int, string> buildEncodingMap(HuffmanNode* encodingTree) {
    Map<int, string> encodingMap;
    buildMapRec(encodingMap, encodingTree, ""); //build map recursively
    return encodingMap;
}

/*
 * Helper function to write bits to an output stream
 *
 * @param   obitstream, char
 *
 */
void writeBits(obitstream& output, char ch){
    string code = encodingMap.get(ch);
    for(int i = 0; i < code.length(); i++){ //for each bit representing the char
        output.writeBit(charToInteger(code.at(i))); //write bit to output
    }
}

/*
 *
 * This function encodes data from the stream of bits,
 * writing them to to the output file, bit by bit
 *
 * @param  istream, map of int to string, obitstream
 *
 */
void encodeData(istream& input, const Map<int, string>& encodingMap, obitstream& output) {
    char ch;
    while(input.get(ch)) writeBits(output, ch); //while bits from file
    writeBits(output, PSEUDO_EOF);  //signify end of file
}

/*
 *
 * This function decodes the compressed data
 *
 * @param  ibitstream, HuffmanNode pointer, ostream
 *
 */
void decodeData(ibitstream& input, HuffmanNode* encodingTree, ostream& output) {
    HuffmanNode* node = encodingTree;
    if(node->character == PSEUDO_EOF) return;   //return if file is empty

    while(input.size() != 0){
        int bit = input.readBit();  //read one bit
        if(bit == 0) node = node->zero; //"0" means we move to zeroth node
        if(bit == 1) node = node->one;  //"1" means we move to first node
        if(node->character == PSEUDO_EOF) break; //break at end of file
        if(node->character != NOT_A_CHAR){   //if node contains a character
            output.put(node->character);    //write the character to the output file
            node = encodingTree;        //go back to the top of the tree
        }
    }
}

/*
 *
 * This function compresses data
 *
 * @param  istream, HuffmanNode pointer, obitstream
 *
 */
void compress(istream& input, obitstream& output) {
    auto freqTable = buildFrequencyTable(input);    //build a freq. map
    rewindStream(input);    //rewind the input stream
    output << freqTable;    //add the freq. map to the output
    auto tree = buildEncodingTree(freqTable);   //build encoding tree
    auto encodingMap = buildEncodingMap(tree);  //build encoding map
    encodeData(input, encodingMap, output);     //encode data
    freeTree(tree);         //free encoding tree once done
    tree = nullptr;
}

/*
 *
 * This function decompresses data
 *
 * @param  ibitstream, HuffmanNode pointer, ostream
 *
 */
void decompress(ibitstream& input, ostream& output) {
    Map<int, int> freqTable;
    input >> freqTable;     //add freq. map to input
    auto tree = buildEncodingTree(freqTable);   //build encoding tree
    decodeData(input, tree, output);    //decode data
    freeTree(tree);     //free encoding tree once done
    tree = nullptr;
}
