export function parse_FEN(fen) 
{
    let board_length_x = window.chessBoardCanvas.boardLengthX;
    let board_length_y = window.chessBoardCanvas.boardLengthY;
    const rows = fen.split('/');
    if (rows.length !== board_length_x) 
    {
        throw new Error(`Invalid FEN format: input must have exactly ${board_length_x} rows.`);
    }

    return rows.map(row => {
        let parsedRow = [];

        // Parse each character in the row
        for (let char of row) 
        {
        	if(isNaN(char))
            {
                if ("BCDKNPSQRUWYbcdknpsqruwy".includes(char)) 
                {
                    parsedRow.push(char);
            	}
                else
                {
              	    throw new Error('Invalid FEN format: invalid piece:'+ char);
                }
            }
            else
            {
                let emptySquares = parseInt(char);
                parsedRow.push(...Array(emptySquares).fill('1'));
            }
        }
        if (parsedRow.length != board_length_y)
        {
            throw new Error(`Invalid FEN format: each row must have exactly ${board_length_y} squares. `+row);
        }
        return parsedRow;
    });
}
